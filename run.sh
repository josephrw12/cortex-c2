#!/bin/bash

# =============================================
# Startup Script - Runs each process in its own CWD
# =============================================

PROJECT_ROOT="$(pwd)"
LOG_DIR="${PROJECT_ROOT}/startup_logs"

mkdir -p "${LOG_DIR}"

echo "Project Root: ${PROJECT_ROOT}"
echo "Logs directory: ${LOG_DIR}"

# ======================
# 1. Start Python Agent
# ======================
PYTHON_DIR="./dist/orchestration"
PYTHON_SCRIPT="main.py"
PYTHON_LOG="${LOG_DIR}/python.log"
PYTHON_PID_FILE="${LOG_DIR}/python_pid.txt"

echo "Launching Python agent from: ${PYTHON_DIR}"

(
    cd "${PYTHON_DIR}" || exit 1
    nohup python3 "${PYTHON_SCRIPT}" > "${PYTHON_LOG}" 2>&1 &
    echo $! > "${PYTHON_PID_FILE}"
)

PYTHON_PID=$(cat "${PYTHON_PID_FILE}" 2>/dev/null || echo "N/A")
echo "Python agent started with PID: ${PYTHON_PID} (CWD: ${PYTHON_DIR})"

# ======================
# 2. Start DB Binary
# ======================
DB_DIR="./db"
DB_BINARY="db_server_2"
DB_LOG="${LOG_DIR}/binary.log"
DB_PID_FILE="${LOG_DIR}/binary_pid.txt"

echo "Launching DB binary from: ${DB_DIR}"

(
    cd "${DB_DIR}" || exit 1
    chmod +x "${DB_BINARY}" 2>/dev/null || true
    nohup "./${DB_BINARY}" > "${DB_LOG}" 2>&1 &
    echo $! > "${DB_PID_FILE}"
)

DB_PID=$(cat "${DB_PID_FILE}" 2>/dev/null || echo "N/A")
echo "DB server started with PID: ${DB_PID} (CWD: ${DB_DIR})"

# ======================
# Summary
# ======================
echo ""
echo "=========================================="
echo "Both services launched successfully!"
echo "=========================================="
echo "Python â†’ CWD: ${PYTHON_DIR} | Log: ${PYTHON_LOG}"
echo "Binary  â†’ CWD: ${DB_DIR}   | Log: ${DB_LOG}"
echo ""
echo "Monitor logs:"
echo "  tail -f ${PYTHON_LOG}"
echo "  tail -f ${DB_LOG}"
echo ""
echo "Stop commands:"
echo "  kill \$(cat ${PYTHON_PID_FILE})"
echo "  kill \$(cat ${DB_PID_FILE})"
