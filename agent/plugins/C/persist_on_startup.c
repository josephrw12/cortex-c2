#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

/*
 * Project layout assumed:
 *
 *   <project_root>/
 *     agent/plugin/C/startup_launcher   <- this binary  (+ source)
 *     agent/plugin/python/communicator.py
 *     db/db_server
 *     startup.sh                        <- extracted here by the binary
 *     logs/                             <- created at runtime
 */

/* Embedded startup script – paths are relative to project root */
static const char *STARTUP_SCRIPT_CONTENT =
"#!/bin/bash\n"
"# startup.sh - Launches db_server and communicator.py in the background\n"
"# Place this file at the project root (same level as db/ and agent/).\n"
"\n"
"# Resolve the directory where THIS script lives (= project root)\n"
"SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
"\n"
"DB_SERVER=\"$SCRIPT_DIR/db/db_server\"\n"
"PY_COMMUNICATOR=\"$SCRIPT_DIR/agent/plugin/python/communicator.py\"\n"
"LOG_DIR=\"$SCRIPT_DIR/logs\"\n"
"\n"
"mkdir -p \"$LOG_DIR\"\n"
"\n"
"# -- db_server ----------------------------------------------------------------\n"
"if [ ! -f \"$DB_SERVER\" ]; then\n"
"    echo \"[ERROR] db_server not found at: $DB_SERVER\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"if [ ! -x \"$DB_SERVER\" ]; then\n"
"    chmod +x \"$DB_SERVER\"\n"
"fi\n"
"\n"
"nohup \"$DB_SERVER\" >> \"$LOG_DIR/db_server.log\" 2>&1 &\n"
"DB_PID=$!\n"
"echo \"[OK] db_server started  (PID $DB_PID)\"\n"
"echo $DB_PID > \"$LOG_DIR/db_server.pid\"\n"
"\n"
"# -- communicator.py ----------------------------------------------------------\n"
"if [ ! -f \"$PY_COMMUNICATOR\" ]; then\n"
"    echo \"[ERROR] communicator.py not found at: $PY_COMMUNICATOR\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"PYTHON_BIN=$(command -v python3 || command -v python)\n"
"if [ -z \"$PYTHON_BIN\" ]; then\n"
"    echo \"[ERROR] No Python interpreter found in PATH\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"nohup \"$PYTHON_BIN\" \"$PY_COMMUNICATOR\" >> \"$LOG_DIR/communicator.log\" 2>&1 &\n"
"PY_PID=$!\n"
"echo \"[OK] communicator.py started (PID $PY_PID)\"\n"
"echo $PY_PID > \"$LOG_DIR/communicator.pid\"\n"
"\n"
"echo \"Startup complete. Logs in $LOG_DIR\"\n";

/* --------------------------------------------------------------------------- */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int write_script(const char *script_path) {
    struct stat st;
    if (stat(script_path, &st) == 0) {
        printf("[INFO] Script already exists at %s - skipping write.\n", script_path);
        return 0;
    }

    FILE *f = fopen(script_path, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot create %s: %s\n", script_path, strerror(errno));
        return -1;
    }

    fputs(STARTUP_SCRIPT_CONTENT, f);
    fclose(f);

    if (chmod(script_path, 0755) != 0)
        fprintf(stderr, "[WARN] chmod failed on %s: %s\n", script_path, strerror(errno));

    printf("[OK] Wrote startup script to %s\n", script_path);
    return 0;
}

/* --------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    /* Resolve the absolute path of this binary */
    char self_path[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len < 0) die("readlink /proc/self/exe");
    self_path[len] = '\0';

    /* bin_dir = .../agent/plugin/C */
    char *bin_dir = dirname(self_path);

    /*
     * Walk three levels up:
     *   agent/plugin/C  ->  agent/plugin  ->  agent  ->  <project_root>
     */
    char project_root_rel[8192];
    snprintf(project_root_rel, sizeof(project_root_rel), "%s/../../..", bin_dir);

    char resolved_root[4096];
    if (realpath(project_root_rel, resolved_root) == NULL)
        die("realpath: could not resolve project root");

    /* startup.sh lives at the project root */
    char script_path[8192];
    snprintf(script_path, sizeof(script_path), "%s/startup.sh", resolved_root);

    /* Optional override: ./startup_launcher /custom/path/startup.sh */
    const char *target = (argc > 1) ? argv[1] : script_path;

    printf("=== Startup Launcher ===\n");
    printf("Binary dir   : %s\n", bin_dir);
    printf("Project root : %s\n", resolved_root);
    printf("Script path  : %s\n\n", target);

    /* 1. Extract the embedded script if not already present */
    if (write_script(target) != 0) {
        fprintf(stderr, "Failed to prepare startup script.\n");
        return EXIT_FAILURE;
    }

    /* 2. Hand off execution to the script (replaces this process) */
    printf("Running startup script...\n\n");
    execl("/bin/bash", "bash", target, (char *)NULL);
    die("execl");   /* only reached on error */
    return EXIT_FAILURE;
}
