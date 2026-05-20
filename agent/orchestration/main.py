#!/usr/bin/env python3
"""
Procedural Modular Reader-Executor Client
"""

import time
import sys
import config
import machine
import db_client
import executor


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print("Usage: python3 main.py")
        sys.exit(0)

    machine_id = machine.initialize_machine()
    last_command = None

    print(f"Executor started for machine: {machine_id}")

    while True:
        try:
            record = db_client.read_last_record(machine_id)
            command = record.get("command")
            record_uuid = record.get("uuid")

            if command and command != last_command:
                print(f"[Main] Executing new command: {command}")
                output = executor.execute_command(command)
                db_client.update_result(record_uuid, output)
                
            last_command = command

            time.sleep(config.SLEEP_INTERVAL)
            

        except KeyboardInterrupt:
            print("\n\nShutdown requested. Exiting...")
            break
        except Exception as e:
            print(f"Unexpected error: {e}")
            time.sleep(5)


if __name__ == "__main__":
    main()
