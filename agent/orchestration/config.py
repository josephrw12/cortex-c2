"""Configuration for the Executor Client"""
# Enter the Data Base server and Port here
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 9000
RECV_SIZE = 4096

# C Binaries
C_BINARY_RUN_SYSTEM_COMMAND = "../plugins/C/run_system_command"
PERSIST_BINARY = "../plugins/C/persist_on_startup"
PRIV_ESC_BINARY = "../plugins/C/priv_esc_CVE-2026-43284"

# On-demand Plugins
ON_DEMAND_DIR = "../on_demand_plugins"
DOWNLOAD_BASE_URL = "http://127.0.0.1:5000/download/"   # <-- Change this

# Lateral Movement
LATERAL_MOVEMENT_BINARY = "../plugins/go/lateral_movement/main"

# General Settings
MACHINE_FILE = "machine.txt"
SLEEP_INTERVAL = 20
COMMAND_TIMEOUT = 60
LOG_FILE = "log.txt"
