#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

/*
 * Project layout:
 *   <project_root>/
 *     agent/plugin/C/persist_on_startup   <- this binary
 *     agent/orchestartion/main.py
 *     db/db_server_2
 *     logs/
 */

static const char *STARTUP_SCRIPT_CONTENT =
"#!/bin/bash\n"
"# startup.sh - Launches db_server_2 and main.py with correct CWDs\n"
"\n"
"SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
"\n"
"DB_BIN=\"$SCRIPT_DIR/db/db_server_2\"\n"
"PY_SCRIPT=\"$SCRIPT_DIR/agent/orchestartion/main.py\"\n"
"LOG_DIR=\"$SCRIPT_DIR/logs\"\n"
"\n"
"mkdir -p \"$LOG_DIR\"\n"
"\n"
"# ====================== db_server_2 ======================\n"
"if [ ! -f \"$DB_BIN\" ]; then\n"
"    echo \"[ERROR] db_server_2 not found at: $DB_BIN\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"if [ ! -x \"$DB_BIN\" ]; then\n"
"    chmod +x \"$DB_BIN\"\n"
"fi\n"
"\n"
"# Run db_server_2 with ./db/ as CWD (as requested)\n"
"DB_DIR=\"$SCRIPT_DIR/db\"\n"
"cd \"$DB_DIR\" || exit 1\n"
"nohup \"$DB_BIN\" >> \"$LOG_DIR/db_server_2.log\" 2>&1 &\n"
"DB_PID=$!\n"
"echo \"[OK] db_server_2 started (PID $DB_PID) - CWD: $DB_DIR\"\n"
"echo $DB_PID > \"$LOG_DIR/db_server_2.pid\"\n"
"\n"
"# ====================== main.py ======================\n"
"if [ ! -f \"$PY_SCRIPT\" ]; then\n"
"    echo \"[ERROR] main.py not found at: $PY_SCRIPT\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"PYTHON_BIN=$(command -v python3 || command -v python)\n"
"if [ -z \"$PYTHON_BIN\" ]; then\n"
"    echo \"[ERROR] No Python interpreter found\" >&2\n"
"    exit 1\n"
"fi\n"
"\n"
"# Run main.py with its own directory as CWD\n"
"PY_DIR=\"$(dirname \"$PY_SCRIPT\")\"\n"
"cd \"$PY_DIR\" || exit 1\n"
"nohup \"$PYTHON_BIN\" \"$PY_SCRIPT\" >> \"$LOG_DIR/communicator.log\" 2>&1 &\n"
"PY_PID=$!\n"
"echo \"[OK] main.py started (PID $PY_PID) - CWD: $PY_DIR\"\n"
"echo $PY_PID > \"$LOG_DIR/communicator.pid\"\n"
"\n"
"echo \"Startup complete. Logs in $LOG_DIR\"\n";

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int write_script(const char *script_path) {
    struct stat st;
    if (stat(script_path, &st) == 0) {
        printf("[INFO] Script already exists at %s\n", script_path);
        return 0;
    }

    FILE *f = fopen(script_path, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot create %s: %s\n", script_path, strerror(errno));
        return -1;
    }

    fputs(STARTUP_SCRIPT_CONTENT, f);
    fclose(f);

    chmod(script_path, 0755);
    printf("[OK] Wrote startup script to %s\n", script_path);
    return 0;
}

int main(int argc, char *argv[]) {
    char self_path[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len < 0) die("readlink /proc/self/exe");
    self_path[len] = '\0';

    char *bin_dir = dirname(self_path);

    /* Go up to project root: agent/plugin/C → project_root */
    char project_root_rel[8192];
    snprintf(project_root_rel, sizeof(project_root_rel), "%s/../../..", bin_dir);

    char resolved_root[4096];
    if (realpath(project_root_rel, resolved_root) == NULL)
        die("realpath: could not resolve project root");

    char script_path[8192];
    snprintf(script_path, sizeof(script_path), "%s/startup.sh", resolved_root);

    const char *target = (argc > 1) ? argv[1] : script_path;

    printf("=== Startup Launcher ===\n");
    printf("Binary dir   : %s\n", bin_dir);
    printf("Project root : %s\n", resolved_root);
    printf("Script path  : %s\n\n", target);

    if (write_script(target) != 0) {
        fprintf(stderr, "Failed to prepare startup script.\n");
        return EXIT_FAILURE;
    }

    printf("Running startup script...\n\n");
    execl("/bin/bash", "bash", target, (char *)NULL);
    die("execl");
    return EXIT_FAILURE;
}
