"""Command execution logic"""

import subprocess
import sys
from config import C_BINARY_RUN_SYSTEM_COMMAND
from utils import sanitize_output
from plugins import is_special_command, execute_special_command


def execute_command(command: str) -> str:
    """Main execution dispatcher."""
    print(f"[Executor] Running: {command!r}")

    if is_special_command(command):
        return execute_special_command(command)

    # Default: Send to C binary
    try:
        proc = subprocess.Popen(
            [C_BINARY_RUN_SYSTEM_COMMAND],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        stdout, stderr = proc.communicate(input=command + "\n")

        if stderr:
            print(stderr, end="", file=sys.stderr)

        if proc.returncode != 0:
            return f"ERROR: C binary failed with code {proc.returncode}"

        return sanitize_output(stdout)

    except Exception as e:
        return f"ERROR: {e}"