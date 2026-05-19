/*
 * db_server.c
 * JSON flat-file DB over a custom TCP application-layer protocol
 *
 * PROTOCOL (application layer, text-based, over raw TCP):
 * ─────────────────────────────────────────────────────────
 * Every message is a single line (ends with \n).
 * Requests  →  VERB|arg1|arg2|...\n
 * Responses →  OK|payload\n   or   ERR|reason\n
 *
 * For multi-row responses (READ_ALL, READ_WHERE) the payload is a
 * JSON array serialised onto one line:
 *   OK|[{...},{...},...]\n
 * An empty result set returns:
 *   OK|[]\n
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
 *   READ_ALL
 *       Return every record in the DB as a JSON array.
 *       Response: OK|[{...},{...},...]
 *
 *   READ_WHERE|<key>|<value>
 *       Return all records where JSON field <key> == <value>.
 *       Response: OK|[{...},{...},...]   or   ERR|NOT_FOUND
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

/* ── tunables ────────────────────────────────────────────── */
#define DB_FILE   "store.json"
#define PORT      9000
#define BACKLOG   8
#define BUF_SIZE  4096

/* ── UUID v4 ─────────────────────────────────────────────── */
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

/* ── tiny JSON helpers ───────────────────────────────────── */

/*
 * Build one JSON record line (no internal newlines).
 * Caller must free() the returned string.
 */
static char *make_record(const char *uuid,
                         const char *machine,
                         const char *command,
                         const char *result)
{
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

/* ── DB operations ────────────────────────────────────────── */

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
 * db_read_all – collect every record from DB_FILE into a
 * dynamically-allocated array of heap strings.
 *
 * On success, *lines_out points to a malloc'd array of malloc'd
 * strings and *count_out holds the number of entries.
 * Caller must free each string and then the array.
 * Returns  0 on success (even when the file is empty),
 *         -1 on I/O error.
 */
static int db_read_all(char ***lines_out, size_t *count_out)
{
    *lines_out  = NULL;
    *count_out  = 0;

    FILE *f = fopen(DB_FILE, "r");
    if (!f) {
        /* treat a missing file as an empty DB, not an error */
        if (errno == ENOENT) return 0;
        return -1;
    }

    char    line[BUF_SIZE];
    char  **arr   = NULL;
    size_t  count = 0;

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;          /* skip blank lines */

        char **tmp = realloc(arr, (count + 1) * sizeof(char *));
        if (!tmp) {
            for (size_t i = 0; i < count; i++) free(arr[i]);
            free(arr);
            fclose(f);
            return -1;
        }
        arr = tmp;
        arr[count] = strdup(line);
        count++;
    }
    fclose(f);

    *lines_out = arr;
    *count_out = count;
    return 0;
}

/*
 * db_read_where – like db_read_all but filtered.
 * Returns only records where json field <key> == <value>.
 *
 * Ownership semantics identical to db_read_all.
 * Returns  0 on success (empty result set is valid),
 *         -1 on I/O error.
 */
static int db_read_where(const char *key, const char *value,
                         char ***lines_out, size_t *count_out)
{
    char  **all   = NULL;
    size_t  total = 0;

    if (db_read_all(&all, &total) < 0) return -1;

    char  **arr   = NULL;
    size_t  count = 0;
    char    field[1024];

    for (size_t i = 0; i < total; i++) {
        if (json_get(all[i], key, field, sizeof field) &&
            strcmp(field, value) == 0)
        {
            char **tmp = realloc(arr, (count + 1) * sizeof(char *));
            if (!tmp) {
                for (size_t j = 0; j < count; j++) free(arr[j]);
                free(arr);
                for (size_t j = i; j < total; j++) free(all[j]);
                free(all);
                return -1;
            }
            arr = tmp;
            arr[count] = all[i];   /* transfer ownership */
            all[i] = NULL;
            count++;
        } else {
            free(all[i]);
        }
    }
    free(all);

    *lines_out = arr;
    *count_out = count;
    return 0;
}

/*
 * Serialise a set of JSON lines into a single-line JSON array string.
 * Returns a malloc'd string; caller must free().
 * e.g.  [{...},{...}]
 */
static char *build_json_array(char **lines, size_t count)
{
    /* first pass: measure required capacity */
    size_t cap = 3;   /* "[]" + NUL */
    for (size_t i = 0; i < count; i++)
        cap += strlen(lines[i]) + 1;   /* +1 for ',' */

    char *buf = malloc(cap);
    if (!buf) return NULL;

    buf[0] = '[';
    size_t pos = 1;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(lines[i]);
        memcpy(buf + pos, lines[i], len);
        pos += len;
        if (i + 1 < count) buf[pos++] = ',';
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';
    return buf;
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

    for (size_t i = 0; i < count; i++) {
        char uuid[64];
        if (!json_get(lines[i], "uuid", uuid, sizeof uuid)) continue;
        if (strcmp(uuid, target_uuid) != 0) continue;

        char machine[256], command[1024], old_result[1024];
        json_get(lines[i], "machine",  machine,    sizeof machine);
        json_get(lines[i], "command",  command,    sizeof command);
        json_get(lines[i], "result",   old_result, sizeof old_result);
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

/* ── dynamic response buffer helpers ─────────────────────── */

/*
 * Write a potentially-large response into a heap buffer.
 * Returns a malloc'd string (caller must free) or NULL on OOM.
 * Format:  "OK|<json_array>\n"
 */
static char *make_array_response(char **lines, size_t count)
{
    char *arr = build_json_array(lines, count);
    if (!arr) return NULL;

    /* "OK|" (3) + array + "\n" (1) + NUL (1) */
    size_t rlen = 3 + strlen(arr) + 2;
    char  *resp = malloc(rlen);
    if (!resp) { free(arr); return NULL; }
    snprintf(resp, rlen, "OK|%s\n", arr);
    free(arr);
    return resp;
}

/* ── protocol dispatcher ─────────────────────────────────── */

/*
 * Process one request line.
 *
 * For most verbs the response fits in resp_buf (stack-allocated by the
 * caller). For READ_ALL / READ_WHERE the response can be arbitrarily
 * large; in that case *heap_resp is set to a malloc'd string and the
 * caller is responsible for free()ing it.  resp_buf is not used when
 * *heap_resp is set.
 *
 * Returns byte-length of the response (excluding NUL).
 */
static size_t handle_request(const char *req,
                             char       *resp_buf,
                             size_t      resp_sz,
                             char      **heap_resp)
{
    *heap_resp = NULL;

    char work[BUF_SIZE];
    strncpy(work, req, sizeof work - 1);
    work[sizeof work - 1] = '\0';
    work[strcspn(work, "\r\n")] = '\0';

    char *tokens[8];
    int   ntok = 0;
    char *p = strtok(work, "|");
    while (p && ntok < 8) { tokens[ntok++] = p; p = strtok(NULL, "|"); }

    if (ntok == 0)
        return (size_t)snprintf(resp_buf, resp_sz, "ERR|EMPTY_REQUEST\n");

    const char *verb = tokens[0];

    /* ── PING ────────────────────────────────────────────── */
    if (strcmp(verb, "PING") == 0)
        return (size_t)snprintf(resp_buf, resp_sz, "OK|PONG\n");

    /* ── WRITE|machine|command ───────────────────────────── */
    if (strcmp(verb, "WRITE") == 0) {
        if (ntok < 3)
            return (size_t)snprintf(resp_buf, resp_sz,
                                    "ERR|WRITE requires machine and command\n");
        char uuid[37];
        if (db_write(tokens[1], tokens[2], uuid) != 0)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_WRITE_FAILED\n");
        return (size_t)snprintf(resp_buf, resp_sz, "OK|%s\n", uuid);
    }

    /* ── READ_LAST|machine ───────────────────────────────── */
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

    /* ── READ_ALL ─────────────────────────────────────────
     *
     * Returns every record as a JSON array on one line:
     *   OK|[{...},{...},...]\n
     * An empty DB returns:
     *   OK|[]\n
     */
    if (strcmp(verb, "READ_ALL") == 0) {
        char  **lines = NULL;
        size_t  count = 0;
        if (db_read_all(&lines, &count) < 0)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_ERROR\n");

        char *resp = make_array_response(lines, count);
        for (size_t i = 0; i < count; i++) free(lines[i]);
        free(lines);

        if (!resp)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|OUT_OF_MEMORY\n");

        *heap_resp = resp;
        return strlen(resp);
    }

    /* ── READ_WHERE|key|value ─────────────────────────────
     *
     * Returns all records where the JSON field <key> == <value>.
     *   OK|[{...},{...},...]\n
     * No matches:
     *   ERR|NOT_FOUND\n
     *
     * Examples:
     *   READ_WHERE|machine|server-01
     *   READ_WHERE|result|success
     *   READ_WHERE|uuid|3f2504e0-4f89-41d3-9a0c-0305e82c3301
     */
    if (strcmp(verb, "READ_WHERE") == 0) {
        if (ntok < 3)
            return (size_t)snprintf(resp_buf, resp_sz,
                                    "ERR|READ_WHERE requires key and value\n");

        char  **lines = NULL;
        size_t  count = 0;
        if (db_read_where(tokens[1], tokens[2], &lines, &count) < 0)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|DB_ERROR\n");

        if (count == 0) {
            free(lines);
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|NOT_FOUND\n");
        }

        char *resp = make_array_response(lines, count);
        for (size_t i = 0; i < count; i++) free(lines[i]);
        free(lines);

        if (!resp)
            return (size_t)snprintf(resp_buf, resp_sz, "ERR|OUT_OF_MEMORY\n");

        *heap_resp = resp;
        return strlen(resp);
    }

    /* ── UPDATE_RESULT|uuid|result ───────────────────────── */
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

/* ── TCP server ──────────────────────────────────────────── */

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

    char  *heap_resp = NULL;
    size_t rlen = handle_request(req, resp, sizeof resp, &heap_resp);

    if (heap_resp) {
        /* large response lives on the heap */
        write(cfd, heap_resp, rlen);
        free(heap_resp);
    } else {
        write(cfd, resp, rlen);
    }

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
    printf("[db_server] Protocol verbs: PING | WRITE | READ_LAST | "
           "READ_ALL | READ_WHERE | UPDATE_RESULT\n");
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
