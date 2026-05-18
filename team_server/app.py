#!/usr/bin/env python3
"""
app.py
Flask web server that exposes the writer_client TCP logic via a REST API.

Endpoints:
    POST /write   JSON body: { "machine_id": "...", "command": "..." }
                  Returns:   { "status": "ok",  "uuid": "..." }
                          or { "status": "error", "message": "..." }

    GET  /        Serves the frontend HTML page.

Run:
    pip install flask flask-cors
    python3 app.py
"""

import socket
import os
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

app = Flask(__name__, static_folder="static")
CORS(app)  # allow the HTML page to call the API freely

# â”€â”€ TCP server config (same as the original writer_client) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
SERVER_HOST = os.environ.get("TCP_HOST", "127.0.0.1")
SERVER_PORT = int(os.environ.get("TCP_PORT", 9000))
RECV_SIZE   = 4096


# â”€â”€ low-level TCP helper (unchanged from writer_client.py) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
def send_tcp_request(host: str, port: int, message: str) -> str:
    """
    Open a TCP connection, send *message* (adds trailing newline if absent),
    read the single-line response, and return it stripped.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(10)          # don't hang forever if server is down
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


# â”€â”€ business logic (mirrors write_record from writer_client.py) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
def write_record(machine_id: str, command: str) -> dict:
    """
    Send WRITE|machine_id|command to the TCP server.
    Returns a dict with keys 'status' and either 'uuid' or 'message'.
    """
    if "|" in machine_id or "|" in command:
        return {"status": "error",
                "message": "machine_id and command must not contain '|'"}

    request_str = f"WRITE|{machine_id}|{command}"

    try:
        response = send_tcp_request(SERVER_HOST, SERVER_PORT, request_str)
    except ConnectionRefusedError:
        return {"status": "error",
                "message": f"TCP server not reachable at {SERVER_HOST}:{SERVER_PORT}"}
    except socket.timeout:
        return {"status": "error", "message": "TCP server timed out"}
    except OSError as exc:
        return {"status": "error", "message": str(exc)}

    if response.startswith("OK|"):
        assigned_uuid = response.split("|", 1)[1]
        return {"status": "ok", "uuid": assigned_uuid}
    else:
        return {"status": "error", "message": response}


# â”€â”€ Flask routes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
@app.route("/")
def index():
    """Serve the frontend."""
    return send_from_directory(".", "index.html")


@app.route("/write", methods=["POST"])
def write_endpoint():
    """
    POST /write
    Accepts JSON: { "machine_id": "MACHINE-42", "command": "uname -a" }
    Returns JSON with the server response.
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


# â”€â”€ entry point â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
if __name__ == "__main__":
    print(f"Flask app starting â€” TCP backend: {SERVER_HOST}:{SERVER_PORT}")
    app.run(host="0.0.0.0", port=5000, debug=True)
