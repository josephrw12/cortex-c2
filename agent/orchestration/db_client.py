"""Database operations (procedural)"""

import json
import sys
from network import send_request


def read_last_record(machine_id: str) -> dict:
    """Fetch the latest record for this machine."""
    request = f"READ_LAST|{machine_id}"
    response = send_request(request)

    if not response.startswith("OK|"):
        print(f"[DB] Failed: {response}")
        sys.exit(1)

    json_str = response.split("|", 1)[1]
    try:
        return json.loads(json_str)
    except json.JSONDecodeError as e:
        print(f"[DB] JSON error: {e}")
        sys.exit(1)


def update_result(record_uuid: str, result: str) -> None:
    """Send command result back to server."""
    request = f"UPDATE_RESULT|{record_uuid}|{result}"
    response = send_request(request)

    if response.startswith("OK|"):
        print("[DB] Result updated successfully.")
    else:
        print(f"[DB] Update failed: {response}")
        sys.exit(1)


def write_record(machine_id: str, command: str) -> str:
    """Write a new record and return its UUID."""
    if "|" in machine_id or "|" in command:
        print("ERROR: '|' character not allowed in machine_id or command")
        sys.exit(1)

    request = f"WRITE|{machine_id}|{command}"
    response = send_request(request)

    if response.startswith("OK|"):
        uuid = response.split("|", 1)[1]
        print(f"[DB] Record written. UUID: {uuid}")
        return uuid
    else:
        print(f"[DB] Write failed: {response}")
        sys.exit(1)