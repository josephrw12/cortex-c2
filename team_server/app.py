#!/usr/bin/env python3
"""
app.py
Flask web server that exposes the writer_client TCP logic via a REST API.

Endpoints:
    POST /write
        JSON body: { "machine_id": "...", "command": "..." }
        Returns:   { "status": "ok",  "uuid": "..." }
                or { "status": "error", "message": "..." }

    GET  /records
        Returns all rows in the DB as a JSON array.
        Returns:   { "status": "ok", "records": [{...}, ...] }
                or { "status": "error", "message": "..." }

    POST /records/where
        JSON body: { "machine_id": "..." }
        Returns all rows whose "machine" field matches the supplied ID.
        Returns:   { "status": "ok", "records": [{...}, ...] }
                or { "status": "error", "message": "..." }
                or { "status": "error", "message": "NOT_FOUND" }  (404)

    GET  /
        Serves the frontend HTML page.

Run:
    pip install flask flask-cors
    python3 app.py
"""

import json
import socket
import os
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

app = Flask(__name__, static_folder="static")
CORS(app)  # allow the HTML page to call the API freely

# ── TCP server config ─────────────────────────────────────────────────────────
SERVER_HOST = os.environ.get("TCP_HOST", "127.0.0.1")
SERVER_PORT = int(os.environ.get("TCP_PORT", 9000))
RECV_SIZE   = 65536   # enlarged to accommodate large READ_ALL payloads


# ── low-level TCP helper ──────────────────────────────────────────────────────
def send_tcp_request(host: str, port: int, message: str) -> str:
    """
    Open a TCP connection, send *message* (adds trailing newline if absent),
    read the single-line response, and return it stripped.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(10)
        sock.connect((host, port))
        payload = message if message.endswith("\n") else message + "\n"
        sock.sendall(payload.encode())

        response = b""
        while True:
            chunk = sock.recv(RECV_SIZE)
            if not chunk:
                break
            response += chunk
            if b"\n" in response:   # protocol: one line per response
                break

    return response.decode().strip()


# ── shared TCP error handler ──────────────────────────────────────────────────
def _tcp_call(request_str: str) -> str | None:
    """
    Wrapper around send_tcp_request that catches network errors and returns
    None on failure (sets _tcp_call.error to a human-readable message).
    """
    try:
        return send_tcp_request(SERVER_HOST, SERVER_PORT, request_str)
    except ConnectionRefusedError:
        _tcp_call.error = f"TCP server not reachable at {SERVER_HOST}:{SERVER_PORT}"
    except socket.timeout:
        _tcp_call.error = "TCP server timed out"
    except OSError as exc:
        _tcp_call.error = str(exc)
    return None

_tcp_call.error = ""


# ── business logic ────────────────────────────────────────────────────────────
def write_record(machine_id: str, command: str) -> dict:
    """Send WRITE|machine_id|command to the TCP server."""
    if "|" in machine_id or "|" in command:
        return {"status": "error",
                "message": "machine_id and command must not contain '|'"}

    response = _tcp_call(f"WRITE|{machine_id}|{command}")
    if response is None:
        return {"status": "error", "message": _tcp_call.error}

    if response.startswith("OK|"):
        return {"status": "ok", "uuid": response.split("|", 1)[1]}
    return {"status": "error", "message": response}


def fetch_all_records() -> dict:
    """
    Send READ_ALL to the TCP server.
    Returns { "status": "ok", "records": [<dict>, ...] }
         or { "status": "error", "message": "..." }
    """
    response = _tcp_call("READ_ALL")
    if response is None:
        return {"status": "error", "message": _tcp_call.error}

    if response.startswith("OK|"):
        payload = response.split("|", 1)[1]
        try:
            records = json.loads(payload)
        except json.JSONDecodeError:
            return {"status": "error", "message": "Malformed JSON from TCP server"}
        return {"status": "ok", "records": records}

    return {"status": "error", "message": response}


def fetch_records_by_machine(machine_id: str) -> dict:
    """
    Send READ_WHERE|machine|<machine_id> to the TCP server.
    Returns { "status": "ok", "records": [<dict>, ...] }
         or { "status": "error", "message": "..." }
    """
    if "|" in machine_id:
        return {"status": "error",
                "message": "machine_id must not contain '|'"}

    response = _tcp_call(f"READ_WHERE|machine|{machine_id}")
    if response is None:
        return {"status": "error", "message": _tcp_call.error}

    if response.startswith("OK|"):
        payload = response.split("|", 1)[1]
        try:
            records = json.loads(payload)
        except json.JSONDecodeError:
            return {"status": "error", "message": "Malformed JSON from TCP server"}
        return {"status": "ok", "records": records}

    # propagate ERR|NOT_FOUND etc.
    return {"status": "error", "message": response.split("|", 1)[-1]}


# ── routes ────────────────────────────────────────────────────────────────────
@app.route("/")
def index():
    """Serve the frontend."""
    return send_from_directory(".", "index.html")


@app.route("/write", methods=["POST"])
def write_endpoint():
    """
    POST /write
    Body: { "machine_id": "MACHINE-42", "command": "uname -a" }
    """
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"status": "error", "message": "Request body must be JSON"}), 400

    machine_id = str(data.get("machine_id", "")).strip()
    command    = str(data.get("command",    "")).strip()

    if not machine_id:
        return jsonify({"status": "error", "message": "'machine_id' is required"}), 400
    if not command:
        return jsonify({"status": "error", "message": "'command' is required"}), 400

    result = write_record(machine_id, command)
    http_status = 200 if result["status"] == "ok" else 502
    return jsonify(result), http_status


@app.route("/records", methods=["GET"])
def get_all_records():
    """
    GET /records
    Fetches every row stored in the DB.

    Success (200):
        { "status": "ok", "records": [ { "uuid": "...", "machine": "...",
                                         "command": "...", "result": "..." },
                                        ... ] }
    Error (502):
        { "status": "error", "message": "..." }
    """
    result = fetch_all_records()
    http_status = 200 if result["status"] == "ok" else 502
    return jsonify(result), http_status


@app.route("/records/where", methods=["POST"])
def get_records_by_machine():
    """
    POST /records/where
    Body: { "machine_id": "MACHINE-42" }

    Returns all DB rows whose "machine" field matches the supplied machine_id.

    Success (200):
        { "status": "ok", "records": [ {...}, ... ] }
    Not found (404):
        { "status": "error", "message": "NOT_FOUND" }
    Bad request (400):
        { "status": "error", "message": "..." }
    Server error (502):
        { "status": "error", "message": "..." }
    """
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"status": "error", "message": "Request body must be JSON"}), 400

    machine_id = str(data.get("machine_id", "")).strip()
    if not machine_id:
        return jsonify({"status": "error", "message": "'machine_id' is required"}), 400

    result = fetch_records_by_machine(machine_id)

    if result["status"] == "ok":
        return jsonify(result), 200
    if result.get("message") == "NOT_FOUND":
        return jsonify(result), 404
    return jsonify(result), 502


if __name__ == "__main__":
    print(f"Flask app starting – TCP backend: {SERVER_HOST}:{SERVER_PORT}")
    app.run(host="0.0.0.0", port=5000, debug=True)
