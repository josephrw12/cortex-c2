"""TCP Network communication"""

import socket
from config import SERVER_HOST, SERVER_PORT, RECV_SIZE


def send_request(message: str) -> str:
    """Send message to server and return response."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((SERVER_HOST, SERVER_PORT))
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