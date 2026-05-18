/*
 * db_server.c
 * JSON flat-file DB over a custom TCP application-layer protocol
 *
 * PROTOCOL (application layer, text-based, over raw TCP):
 * â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Every message is a single line (ends with \n).
 * Requests  â†’  VERB|arg1|arg2|...\n
 * Responses â†’  OK|payload\n   or   ERR|reason\n
 *
 * Supported verbs:
 *   WRITE|<machine_id>|<command>
 *       Append a new JSON record.  Server auto-generates UUID.
 *       Response: OK|<uuid>
 *
 *   READ_LAST|<machine_id>
 *       Return the last record whose "machine" matches <machine_id>.
 *       Response: OK|<json_line>   or   ERR|NOT_FOUND
 *
 *   UPDATE_RESULT|<uuid>|<result>
 *       Set the "result" field of the record with the given uuid.
 *       Response: OK|UPDATED   or   ERR|NOT_FOUND
 *
 *   PING
 *       Health-check.  Response: OK|PONG
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* â”€â”€ tunables â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
#define DB_FILE   "store.json"
#define PORT      9000
#define BACKLOG   8
#define BUF_SIZE  4096

/* â”€â”€ UUID v4 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void generate_uuid(char out[37])
{
    unsigned char b[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { perror("open /dev/urandom"); exit(1); }
    read(fd, b, 16);
    close(fd);
    b[6] = (b[6] & 0x0f) | 0x40;   /* version 4 */
    b[8] = (b[8] & 0x3f) | 0x80;   /* variant  */
    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x"
        "-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

/* â”€â”€ tiny JSON helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Build one JSON record line (no internal newlines).
 * Caller must free() the returned string.
 */
static char *make_record(const char *uuid,
                         const char *machine,
                         const char *command,
                         const char *result)
{
    /* {"uuid":"...","machine":"...","command":"...","result":"..."} */
    size_t n = strlen(uuid) + strlen(machine) +
               strlen(command) + strlen(result) + 64;
    char *buf = malloc(n);
    snprintf(buf, n,
        "{\"uuid\":\"%s\",\"machine\":\"%s\","
        "\"command\":\"%s\",\"result\":\"%s\"}",
        uuid, machine, command, result);
    return buf;
}

/*
 * Extract the value of "key" from a simple flat JSON object.
 * Works for plain string values (no nested objects / escapes).
 * Returns 1 on success, 0 on failure.
 */
static int json_get(const char *json, const char *key, char *out, size_t outsz)
{
    /* look for  "key":"  */
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    const char *end = strchr(p, '"');
    if (!end) return 0;
    size_t len = (size_t)(end - p);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/* â”€â”€ DB operations â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/* Append one JSON record line to DB_FILE */
static int db_write(const char *machine, const char *command, char uuid_out[37])
{
    generate_uuid(uuid_out);
    char *rec = make_record(uuid_out, machine, command, "");

    FILE *f = fopen(DB_FILE, "a");
    if (!f) { free(rec); return -1; }
    fprintf(f, "%s\n", rec);
    fclose(f);
    free(rec);
    return 0;
}

/*
 * Read the last line whose "machine" field == target.
 * Copies the whole JSON line into `out` (caller provides outsz bytes).
 * Returns 1 found / 0 not-found / -1 error.
 */
static int db_read_last(const char *machine, char *out, size_t outsz)
{
    FILE *f = fopen(DB_FILE, "r");
    if (!f) return -1;

    char line[BUF_SIZE];
    char match[BUF_SIZE];
    int  found = 0;
    char m[256];

    while (fgets(line, sizeof line, f)) {
        /* strip newline */
        line[strcspn(line, "\n")] = '\0';
        if (!json_get(line, "machine", m, sizeof m)) continue;
        if (strcmp(m, machine) == 0) {
            strncpy(match, line, sizeof match - 1);
            match[sizeof match - 1] = '\0';
            found = 1;
        }
    }
    fclose(f);

    if (found) {
        strncpy(out, match, outsz - 1);
        out[outsz - 1] = '\0';
        return 1;
    }
    return 0;
}

/*
 * Rewrite DB_FILE, replacing the "result" field of the record
 * whose "uuid" == target_uuid.
 * Returns 1 updated / 0 not-found / -1 error.
 */
static int db_update_result(const char *target_uuid, const char *new_result)
{
    FILE *f = fopen(DB_FILE, "r");
    if (!f) return -1;

    /* read all lines into memory */
    char **lines = NULL;
    size_t count  = 0;
    char   line[BUF_SIZE];
    int    updated = 0;

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        lines = realloc(lines, (count + 1) * sizeof(char *));
        lines[count] = strdup(line);
        count++;
    }
    fclose(f);

    /* patch the matching line */
    for (size_t i = 0; i < count; i++) {
        char uuid[64];
        if (!json_get(lines[i], "uuid", uuid, sizeof uuid)) continue;
        if (strcmp(uuid, target_uuid) != 0) continue;

        /* extract other fields */
        char machine[256], command[1024], old_result[1024];
        json_get(lines[i], "machine",  machine,     sizeof machine);
        json_get(lines[i], "command",  command,     sizeof command);
        json_get(lines[i], "result",   old_result,  sizeof old_result);
        (void)old_result;

        free(lines[i]);
        lines[i] = make_record(uuid, machine, command, new_result);
        updated = 1;
        break;
    }

    if (!updated) {
        for (size_t i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return 0;
    }

    /* rewrite file */
    f = fopen(DB_FILE, "w");
    if (!f) {
        for (size_t i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        fprintf(f, "%s\n", lines[i]);
        free(lines[i]);
    }
    fclose(f);
    free(lines);
    return 1;
}

/* â”€â”€ protocol dispatcher â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Process one request line and write the response into resp_buf.
 * Returns length of response (excluding NUL).
 */
static size_t handle_request(const char *req, char *resp_buf, size_t resp_sz)
{
    /* tokenise by '|' */
    char work[BUF_SIZE];
    strncpy(work, req, sizeof work - 1);
    work[sizeof work - 1] = '\0';
    /* strip trailing \r\n */
    work[strcspn(work, "\r\n")] = '\0';

    char *tokens[8];
    int   ntok = 0;
    char *p = strtok(work, "|");
    while (p && ntok < 8) { tokens[ntok++] = p; p = strtok(NULL, "|"); }

    if (ntok == 0) {
        return (size_t)snprintf(resp_buf, resp_sz, "ERR|EMPTY_REQUEST\n");
    }

    const char *verb = tokens[0];

    /* â”€â”€ PING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (strcmp(verb, "PING") == 0) {
        return (size_t)snprintf(resp_buf, resp_sz, "OK|PONG\n");
    }

    /* â”€â”€ WRITE|machine|command â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (strcmp(verb, "WRITE") == 0) {
        if (ntok < 3)
            return (size_t)snprintf(resp_buf, resp_sz,
                                    "ERR|WRITE requires machine and command\n");
        char uuid[37];
        if (db_write(tokens[1], tokens[2], uuid) != 0)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_WRITE_FAILED\n");
        return (size_t)snprintf(resp_buf, resp_sz, "OK|%s\n", uuid);
    }

    /* â”€â”€ READ_LAST|machine â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (strcmp(verb, "READ_LAST") == 0) {
        if (ntok < 2)
            return (size_t)snprintf(resp_buf, resp_sz,
                                    "ERR|READ_LAST requires machine_id\n");
        char out[BUF_SIZE];
        int r = db_read_last(tokens[1], out, sizeof out);
        if (r  < 0) return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_ERROR\n");
        if (r == 0) return (size_t)snprintf(resp_buf, resp_sz, "ERR|NOT_FOUND\n");
        return (size_t)snprintf(resp_buf, resp_sz, "OK|%s\n", out);
    }

    /* â”€â”€ UPDATE_RESULT|uuid|result â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (strcmp(verb, "UPDATE_RESULT") == 0) {
        if (ntok < 3)
            return (size_t)snprintf(resp_buf, resp_sz,
                                    "ERR|UPDATE_RESULT requires uuid and result\n");
        int r = db_update_result(tokens[1], tokens[2]);
        if (r  < 0) return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_ERROR\n");
        if (r == 0) return (size_t)snprintf(resp_buf, resp_sz, "ERR|NOT_FOUND\n");
        return (size_t)snprintf(resp_buf, resp_sz, "OK|UPDATED\n");
    }

    return (size_t)snprintf(resp_buf, resp_sz, "ERR|UNKNOWN_VERB\n");
}

/* â”€â”€ TCP server â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void serve_client(int cfd)
{
    char req[BUF_SIZE];
    char resp[BUF_SIZE * 2];
    ssize_t n;

    /* read until newline or buffer full */
    size_t total = 0;
    while (total < sizeof req - 1) {
        n = read(cfd, req + total, 1);
        if (n <= 0) goto done;
        total++;
        if (req[total - 1] == '\n') break;
    }
    req[total] = '\0';

    size_t rlen = handle_request(req, resp, sizeof resp);
    write(cfd, resp, rlen);

done:
    close(cfd);
}

int main(void)
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORT)
    };

    if (bind(sfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(sfd); return 1;
    }
    if (listen(sfd, BACKLOG) < 0) {
        perror("listen"); close(sfd); return 1;
    }

    printf("[db_server] Listening on port %d  (DB: %s)\n", PORT, DB_FILE);
    printf("[db_server] Protocol verbs: PING | WRITE | READ_LAST | UPDATE_RESULT\n");
    fflush(stdout);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof cli;
        int cfd = accept(sfd, (struct sockaddr *)&cli, &cli_len);
        if (cfd < 0) { perror("accept"); continue; }
        printf("[db_server] Connection from %s:%d\n",
               inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
        fflush(stdout);
        serve_client(cfd);
    }
}
