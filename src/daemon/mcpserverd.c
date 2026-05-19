/*
 * mcpserverd.c - IRIX MCP server daemon
 *
 * Main entry point for the daemon process. Handles startup sequence,
 * signal management, accept loop, and clean shutdown.
 *
 * Startup sequence (per docs/ARCHITECTURE.md §7):
 *   1. Open syslog
 *   2. Load /etc/mcpserver/boundaries.json
 *   3. Unlink + bind UNIX domain socket
 *   4. Write PID file
 *   5. Install signal handlers
 *   6. Accept loop: read JSON-RPC line, dispatch, write response
 *   7. On SIGTERM: close socket, unlink, remove PID, exit
 */

#include "../core/policy.h"
#include "../core/protocol.h"
#include "../core/json.h"
#include "ipc.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <fcntl.h>

#define BOUNDARIES_PATH  "/etc/mcpserver/boundaries.json"
#define PID_PATH         "/var/run/mcpserverd.pid"

/* ------------------------------------------------------------------ */
/* Signal handling                                                      */
/* ------------------------------------------------------------------ */

static volatile int g_running  = 1;
static volatile int g_reload   = 0;

static void
sig_term(int sig)
{
    (void)sig;
    g_running = 0;
}

static void
sig_hup(int sig)
{
    (void)sig;
    g_reload = 1;
}

static void
sig_chld(int sig)
{
    /* reap children to prevent zombies (future: forked handlers) */
    int status;
    (void)sig;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

static void
install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sig_term;
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = sig_hup;
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = sig_chld;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* ------------------------------------------------------------------ */
/* PID file                                                             */
/* ------------------------------------------------------------------ */

static void
write_pid(void)
{
    FILE *f = fopen(PID_PATH, "w");
    if (!f) {
        syslog(LOG_WARNING, "mcpserverd: cannot write PID file %s: %m",
               PID_PATH);
        return;
    }
    fprintf(f, "%ld\n", (long)getpid());
    fclose(f);
}

static void
remove_pid(void)
{
    unlink(PID_PATH);
}

/* ------------------------------------------------------------------ */
/* Per-connection handler                                               */
/* ------------------------------------------------------------------ */

static void
handle_connection(int client_fd, const struct policy *p)
{
    struct protocol_ctx    ctx;
    struct jsonrpc_request req;
    char                   line[JSON_MSG_MAX];
    char                   resp[PROTO_RESP_MAX];
    int                    n;
    int                    rn;

    protocol_init(&ctx, p);

    while (g_running) {
        n = ipc_read_line(client_fd, line, sizeof(line));
        if (n == 0) {
            syslog(LOG_INFO, "mcpserverd: client disconnected");
            break;
        }
        if (n < 0) {
            if (errno != EINTR)
                syslog(LOG_ERR, "mcpserverd: read error: %m");
            break;
        }
        if (n == 0) continue; /* blank line */

        if (jsonrpc_parse(line, &req) != 0) {
            jsonrpc_write_error(resp, sizeof(resp), "", 1,
                                JSONRPC_PARSE_ERROR, "parse error");
            ipc_write_all(client_fd, resp, (int)strlen(resp));
            continue;
        }

        rn = protocol_handle(&ctx, &req, resp, PROTO_RESP_MAX);
        if (rn > 0)
            ipc_write_all(client_fd, resp, rn);
    }

    close(client_fd);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    struct policy p;
    int           listen_fd;
    int           client_fd;
    const char   *boundaries = BOUNDARIES_PATH;

    (void)argc; (void)argv;

    openlog("mcpserverd", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "mcpserverd starting (version %s)", MCPSERVER_VERSION);

    /* load policy */
    if (policy_load(boundaries, &p) != 0) {
        syslog(LOG_ERR, "mcpserverd: failed to load policy, aborting");
        closelog();
        return 1;
    }

    /* set up IPC socket */
    listen_fd = ipc_listen();
    if (listen_fd < 0) {
        syslog(LOG_ERR, "mcpserverd: failed to open socket, aborting");
        closelog();
        return 1;
    }

    write_pid();
    install_signals();

    syslog(LOG_INFO, "mcpserverd: ready, waiting for connections");

    /* main accept loop — fork for each connection so multiple stdio
     * bridges (one per Claude Code / Codex session) work concurrently.
     * SIGCHLD handler reaps finished children. */
    while (g_running) {
        pid_t child;

        if (g_reload) {
            g_reload = 0;
            syslog(LOG_INFO, "mcpserverd: reloading policy");
            policy_load(boundaries, &p);
        }

        client_fd = ipc_accept(listen_fd);
        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }

        child = fork();
        if (child < 0) {
            syslog(LOG_ERR, "mcpserverd: fork failed: %m");
            close(client_fd);
            continue;
        }

        if (child == 0) {
            /* child: close the listening socket, handle this client */
            close(listen_fd);
            handle_connection(client_fd, &p);
            exit(0);
        }

        /* parent: close client fd and loop back to accept */
        close(client_fd);
        syslog(LOG_INFO, "mcpserverd: spawned handler pid %ld", (long)child);
    }

    syslog(LOG_INFO, "mcpserverd: shutting down");
    ipc_close_listener(listen_fd);
    remove_pid();
    closelog();
    return 0;
}
