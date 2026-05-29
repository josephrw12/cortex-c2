/* Feature-test macros â€” MUST come before any system header */
#define _XOPEN_SOURCE  600   /* unlocks posix_openpt, grantpt, unlockpt, ptsname */
#define _GNU_SOURCE          /* unlocks ptsname_r and other GNU extensions        */

#include <libwebsockets.h>
#include <sys/select.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pty.h>      /* forkpty, openpty â€” also pulls in correct PTY prototypes  */

#include <curl/curl.h>

#define BUF_SIZE    4096
#define MAX_SNAME   1000
#define WS_HOST     "127.0.0.1"
#define WS_PORT     8765
#define WS_PATH     "/"

/* ==================== Globals ==================== */
static struct lws         *client_wsi = NULL;
static struct lws_context *ws_context = NULL;
static int                 interrupted = 0;
static int                 masterFd   = -1;
static int                 scriptFd   = -1;
static struct termios      ttyOrig;

static char  binary_path[512] = {0};
//static int   binary_ready     = 0;   /* set to 1 after download+chmod done */

static pid_t  childPid = -1;
static int    maxFd    = 0;


static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return fwrite(ptr, size, nmemb, (FILE *)userdata);
}

static int download_file(const char *url, const char *dest_path) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return -1; }

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,    1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) { unlink(dest_path); return -1; }
    return 0;
}

/* ==================== Error helper ==================== */
static void err_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

/* ==================== TTY helpers ==================== */
static void ttySetRaw(int fd, struct termios *prevTermios)
{
    struct termios t;

    if (tcgetattr(fd, &t) == -1)
        err_exit("tcgetattr");

    if (prevTermios != NULL)
        *prevTermios = t;

    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
    t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
                   INPCK  | ISTRIP | IXON   | PARMRK);
    t.c_oflag &= ~OPOST;
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &t) == -1)
        err_exit("tcsetattr");
}

static void ttyReset(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &ttyOrig);
}

/* ==================== PTY Fork ==================== */
static pid_t ptyFork(int *masterFd, char *slaveName, size_t snLen,
                     const struct termios *slaveTermios,
                     const struct winsize  *slaveWS)
{
    int   slaveFd;
    pid_t childPid;

    /*
     * Local buffer for ptsname_r.
     * Avoids the static-buffer issue of ptsname() and guarantees the
     * name is safe to use in the child even after close(*masterFd).
     */
    char localSlaveName[MAX_SNAME];

    *masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (*masterFd == -1)
        err_exit("posix_openpt");

    if (grantpt(*masterFd) == -1)
        err_exit("grantpt");

    if (unlockpt(*masterFd) == -1)
        err_exit("unlockpt");

    /*
     * ptsname_r writes into our OWN buffer before the fork.
     * After close(*masterFd) in the child, ptsname() returns NULL
     * which causes open(NULL) -> segfault. ptsname_r into a stack
     * buffer is immune to that.
     */
    if (ptsname_r(*masterFd, localSlaveName, sizeof(localSlaveName)) != 0)
        err_exit("ptsname_r");

    if (slaveName != NULL)
        snprintf(slaveName, snLen, "%s", localSlaveName);

    childPid = fork();
    if (childPid == -1)
        err_exit("fork");

    /* ---- Parent ---- */
    if (childPid != 0)
        return childPid;

    /* ---- Child ---- */
    if (setsid() == -1)
        err_exit("setsid");

    close(*masterFd);   /* Not needed in child */

    /*
     * Use localSlaveName â€” captured before the fork into our own buffer.
     * NEVER call ptsname() here; *masterFd is closed and it would return NULL.
     */
    slaveFd = open(localSlaveName, O_RDWR);
    if (slaveFd == -1)
        err_exit("open-slave");

#ifdef TIOCSCTTY
    if (ioctl(slaveFd, TIOCSCTTY, 0) == -1)
        err_exit("ioctl-TIOCSCTTY");
#endif

    if (slaveTermios != NULL)
        if (tcsetattr(slaveFd, TCSANOW, slaveTermios) == -1)
            err_exit("tcsetattr");

    if (slaveWS != NULL)
        if (ioctl(slaveFd, TIOCSWINSZ, slaveWS) == -1)
            err_exit("ioctl-TIOCSWINSZ");

    if (dup2(slaveFd, STDIN_FILENO)  == -1 ||
        dup2(slaveFd, STDOUT_FILENO) == -1 ||
        dup2(slaveFd, STDERR_FILENO) == -1)
        err_exit("dup2");

    if (slaveFd > STDERR_FILENO)
        close(slaveFd);

    return 0;
}

/* ==================== WebSocket Callback ==================== */
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    (void)user;

    switch (reason) {

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("[WS] Connected to %s:%d\n", WS_HOST, WS_PORT);
            client_wsi = wsi;
            break;

case LWS_CALLBACK_CLIENT_RECEIVE: 
    if (masterFd == -1 || len == 0) break;

    char msg[512];
    snprintf(msg, sizeof(msg), "%.*s", (int)len, (char *)in);

    if (strncmp(msg, "plugin:", 7) == 0) {
        /* ---- Plugin path: download binary and run in PTY ---- */
        char *plugin_name = msg + 7;   /* everything after "plugin:" */

        /* Build the download URL from the plugin name */
        char url[512];
        snprintf(url, sizeof(url), "https://your-plugin-server.com/plugins/%s", plugin_name);
        snprintf(binary_path, sizeof(binary_path), "/tmp/%s", plugin_name);

        printf("[WS] Plugin requested: %s\n", plugin_name);
        printf("[WS] Downloading from: %s\n", url);

        if (download_file(url, binary_path) != 0) {
            ws_send("ERROR: download failed\n", 22);
            break;
        }
        if (chmod(binary_path, 0700) != 0) {
            ws_send("ERROR: chmod failed\n", 20);
            break;
        }

        /* Kill any existing child before forking a new one */
        if (childPid > 0) {
            kill(childPid, SIGTERM);
            waitpid(childPid, NULL, 0);
            close(masterFd);
            masterFd = -1;
        }

        /* Fork a new PTY running the downloaded binary */
        char slaveName[MAX_SNAME];
        struct winsize ws_size;
        ioctl(STDIN_FILENO, TIOCGWINSZ, &ws_size);

        childPid = ptyFork(&masterFd, slaveName, MAX_SNAME, &ttyOrig, &ws_size);
        if (childPid == 0) {
            char *args[] = { binary_path, NULL };
            char *envp[] = {
                "TERM=xterm-256color",
                "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                NULL
            };
            execve(binary_path, args, envp);
            _exit(127);
        }

		maxFd = masterFd;   /* ? always update after fork, not just if larger */
        /* Update maxFd in case the new masterFd is larger */
        //if (masterFd > maxFd) maxFd = masterFd;

        printf("[WS] Plugin running, PID: %d\n", childPid);

    } else {
        /* ---- Normal path: treat as shell input, write to PTY ---- */
        write(masterFd, in, len);
    }
    break;


        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("[WS] Connection closed by server\n");
            /*
             * NULL out the pointer immediately so the main loop cannot
             * call lws_write() on a freed/dangling wsi.
             */
            client_wsi = NULL;
            interrupted = 1;
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "[WS] Connection error: %s\n",
                    in ? (char *)in : "(unknown)");
            client_wsi = NULL;
            interrupted = 1;
            break;

        default:
            break;
    }

    return 0;
}

static struct lws_protocols protocols[] = {
    { "terminal-protocol", ws_callback, 0, BUF_SIZE },
    { NULL, NULL, 0, 0 }
};

/* ==================== Signal handler ==================== */
static void sigint_handler(int sig)
{
    (void)sig;
    interrupted = 1;
}

/* ==================== WebSocket send helper ==================== */
void ws_send(const char *data, size_t len)
{
    if (!client_wsi || len == 0 || len > BUF_SIZE)
        return;

    /* lws_write requires LWS_PRE bytes of headroom before the payload */
    unsigned char wsbuf[LWS_PRE + BUF_SIZE];
    memcpy(wsbuf + LWS_PRE, data, len);

    if (lws_write(client_wsi, wsbuf + LWS_PRE, len, LWS_WRITE_BINARY) < 0)
        fprintf(stderr, "[WS] lws_write failed\n");
}

/* ==================== Main ==================== */
int main(int argc, char *argv[])
{
    char           slaveName[MAX_SNAME];
    char          *shell;
    //pid_t          childPid;
    struct winsize ws_size;
    fd_set         inFds;
    char           buf[BUF_SIZE];
    ssize_t        numRead;
    //int            maxFd;

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);
    signal(SIGPIPE, SIG_IGN);   /* Prevent crash on broken pipe */

    /* ---- Save original terminal settings ---- */
    if (tcgetattr(STDIN_FILENO, &ttyOrig) == -1)
        err_exit("tcgetattr");
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws_size) < 0)
        err_exit("ioctl-TIOCGWINSZ");

    ttySetRaw(STDIN_FILENO, &ttyOrig);

    if (atexit(ttyReset) != 0)
        err_exit("atexit");

    /* ---- Fork PTY + shell ---- */
    childPid = ptyFork(&masterFd, slaveName, MAX_SNAME, &ttyOrig, &ws_size);
    if (childPid == -1)
        err_exit("ptyFork");

    if (childPid == 0) {        /* Child: exec shell */
        shell = getenv("SHELL");
        if (shell == NULL || *shell == '\0')
            shell = "/bin/sh";
        execlp(shell, shell, NULL);
        err_exit("execlp");
    }

    /* ---- Open session recording file ---- */
    scriptFd = open((argc > 1) ? argv[1] : "typescript",
                    O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (scriptFd == -1)
        err_exit("open typescript");

    /* ---- libwebsockets context ---- */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    /* Plain ws:// â€” no TLS to localhost */

    ws_context = lws_create_context(&info);
    if (!ws_context)
        err_exit("lws_create_context");

    /* ---- Connect to server ---- */
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context        = ws_context;
    connect_info.address        = WS_HOST;
    connect_info.port           = WS_PORT;
    connect_info.path           = WS_PATH;
    connect_info.host           = WS_HOST;
    connect_info.origin         = WS_HOST;
    connect_info.protocol       = protocols[0].name;
    connect_info.ssl_connection = 0;    /* Plain WebSocket, no TLS */

    client_wsi = lws_client_connect_via_info(&connect_info);
    if (!client_wsi) {
        fprintf(stderr, "[WS] Failed to initiate connection to %s:%d\n",
                WS_HOST, WS_PORT);
        lws_context_destroy(ws_context);
        return 1;
    }

    printf("[PTY] Bridge running  (server: ws://%s:%d%s)\n",
           WS_HOST, WS_PORT, WS_PATH);
    printf("[PTY] Recording to  : %s\n", (argc > 1) ? argv[1] : "typescript");

    /* masterFd is always > STDIN_FILENO (0) */
    maxFd = masterFd;

    /* ==================== Main event loop ==================== */
    while (!interrupted) {

        /*
         * Drive the lws event loop with a 5 ms timeout.
         * Using 0 busy-spins the CPU and is deprecated in lws >= 3.x.
         */
        lws_service(ws_context, 5);

        FD_ZERO(&inFds);
        FD_SET(STDIN_FILENO, &inFds);   /* keyboard input */
        FD_SET(masterFd,     &inFds);   /* PTY output     */

        struct timeval tv = { 0, 5000 };   /* 5 ms */

        int ready = select(maxFd + 1, &inFds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ready == 0)
            continue;

        /* ---- Keyboard -> PTY ---- */
        if (FD_ISSET(STDIN_FILENO, &inFds)) {
            numRead = read(STDIN_FILENO, buf, BUF_SIZE);
            if (numRead > 0) {
                if (write(masterFd, buf, numRead) != numRead)
                    perror("write stdin->pty");
            }
        }

        /* ---- PTY output -> stdout + recording file + WebSocket ---- */
        if (FD_ISSET(masterFd, &inFds)) {
            numRead = read(masterFd, buf, BUF_SIZE);
            if (numRead <= 0)   /* shell exited */
                break;

            if (write(STDOUT_FILENO, buf, numRead) != numRead)
                perror("write pty->stdout");

            if (write(scriptFd, buf, numRead) != numRead)
                perror("write pty->script");

            ws_send(buf, (size_t)numRead);
        }
    }

    printf("\n[PTY] Shutting down...\n");
    lws_context_destroy(ws_context);
    close(scriptFd);
    return 0;
}
