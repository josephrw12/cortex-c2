#!/bin/bash
# run.sh - Launches db_server and c2_communicator.py in the background
# Place this file at the project root (same level as db/ and agent/).

# Resolve the directory where THIS script lives (= project root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DB_SERVER="$SCRIPT_DIR/db/db_server"
PY_COMMUNICATOR="$SCRIPT_DIR/agent/plugin/python/c2_communicator.py"
LOG_DIR="$SCRIPT_DIR/logs"

mkdir -p "$LOG_DIR"

# -- db_server ----------------------------------------------------------------
if [ ! -f "$DB_SERVER" ]; then
    echo "[ERROR] db_server not found at: $DB_SERVER" >&2
    exit 1
fi

if [ ! -x "$DB_SERVER" ]; then
    chmod +x "$DB_SERVER"
fi

nohup "$DB_SERVER" >> "$LOG_DIR/db_server.log" 2>&1 &
DB_PID=$!
echo "[OK] db_server started  (PID $DB_PID)"
echo $DB_PID > "$LOG_DIR/db_server.pid"

# -- c2_communicator.py ----------------------------------------------------------
if [ ! -f "$PY_COMMUNICATOR" ]; then
    echo "[ERROR] c2_communicator.py not found at: $PY_COMMUNICATOR" >&2
    exit 1
fi

PYTHON_BIN=$(command -v python3 || command -v python)
if [ -z "$PYTHON_BIN" ]; then
    echo "[ERROR] No Python interpreter found in PATH" >&2
    exit 1
fi

nohup "$PYTHON_BIN" "$PY_COMMUNICATOR" >> "$LOG_DIR/communicator.log" 2>&1 &
PY_PID=$!
echo "[OK] c2_communicator.py started (PID $PY_PID)"
echo $PY_PID > "$LOG_DIR/communicator.pid"

echo "run complete. Logs in $LOG_DIR"
