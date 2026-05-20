"""Machine identity management"""

from datetime import date
import config, os
from utils import read_machine_id, save_machine_id, clean_output
import db_client
import executor

'''
def get_clean_whoami() -> str:
    """Get clean username without any 'Output:' prefix."""
    raw_output = executor.execute_command("whoami")
    
    # Clean common prefixes added by C binary or shell
    clean = raw_output.replace("Output:", "").replace("output:", "").strip()
    # Also remove any extra whitespace or newlines
    clean = " ".join(clean.split())
    
    return clean
'''
def get_clean_whoami() -> str:
    raw = executor.execute_command("whoami")
    return clean_output(raw)

def initialize_machine() -> str:
    """Handle first-time machine setup with clean machine_id."""
    machine_id = read_machine_id()
    if machine_id:
        print(f"[Init] Using existing machine ID: {machine_id}")
        return machine_id

    print("[Init] First run detected. Creating machine identity...")

    # Get clean username
    username = get_clean_whoami()
    today = date.today().isoformat()
    
    #machine_id = f"{username}{today}"
    # Optional: make it more unique
    machine_id = f"{username}-{today}-{os.getpid()}"

    save_machine_id(machine_id)

    print(f"[Init] Machine ID created: {machine_id}")

    # === Create initial record ===
    record_uuid = db_client.write_record(machine_id, "whoami")
    
    # Run whoami again and store clean result
    whoami_result = get_clean_whoami()
    
    db_client.update_result(record_uuid, whoami_result)

    print(f"[Init] Initial 'whoami' record created and updated.")
    return machine_id
