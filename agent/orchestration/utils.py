"""Utility functions"""

import sys
from datetime import date
import config

def clean_output(output: str) -> str:
    """Remove common prefixes and clean whitespace."""
    if not output:
        return ""
    cleaned = output.replace("Output:", "").replace("output:", "").strip()
    return " ".join(cleaned.split())

def sanitize_output(output: str) -> str:
    """Sanitize command output for the protocol."""
    if not output or output.isspace():
        return "(no output)"
    return output.replace("\n", " ").replace("|", ";").strip()


def read_machine_id() -> str | None:
    """Read machine ID from file."""
    try:
        with open(config.MACHINE_FILE, "r", encoding="utf-8") as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def save_machine_id(machine_id: str) -> None:
    """Save machine ID to file."""
    with open(config.MACHINE_FILE, "w", encoding="utf-8") as f:
        f.write(machine_id)
