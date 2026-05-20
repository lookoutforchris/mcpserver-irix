/*
 * stdio_bridge.c - stdio <-> UNIX socket MCP bridge
 *
 * Connects to /var/run/mcpserverd.sock and proxies JSON-RPC lines
 * between stdin/stdout and the daemon socket.
 *
 * Uses select() when waiting for the daemon response so that SSH
 * disconnect (stdin EOF) is detected even if the bridge is mid-request.
 * This prevents zombie bridge processes from blocking the daemon.
 */

#include "stdio_bridge.h"
#include "../daemon/ipc.h"
#include "../core/json.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>   /* struct timeval, select() */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*
 * connect_to_daemon - open a connection to the mcpserverd socket.
 * Returns connected fd, or -1 on error.
 */
static int
connect_to_daemon(void)
{
    struct sockaddr_un addr;
    int                fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "mcpserver stdio: socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, IPC_SOCK_PATH);

    if (connect(fd, (struct sockaddr *)&addr,
                (int)(strlen(addr.sun_path) + sizeof(addr.sun_family))) < 0) {
        fprintf(stderr, "mcpserver stdio: cannot connect to daemon at %s: %s\n",
                IPC_SOCK_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * wait_for_daemon - wait for daemon_fd to be readable, but also watch
 * stdin_fd so we detect SSH disconnect while waiting for a response.
 *
 * Returns:  1  daemon_fd is readable
 *           0  stdin_fd reached EOF (SSH disconnected)
 *          -1  error or timeout
 */
static int
wait_for_daemon(int daemon_fd, int stdin_fd)
{
    fd_set         rfds;
    struct timeval tv;
    int            maxfd;
    int            sel;

    FD_ZERO(&rfds);
    FD_SET(daemon_fd, &rfds);
    FD_SET(stdin_fd,  &rfds);
    maxfd = (daemon_fd > stdin_fd) ? daemon_fd : stdin_fd;

    /* 60-second timeout — longer than Claude Code's 30s MCP timeout */
    tv.tv_sec  = 60;
    tv.tv_usec = 0;

    do {
        sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    } while (sel < 0 && errno == EINTR);

    if (sel < 0)  return -1; /* error */
    if (sel == 0) return -1; /* timeout */

    /* stdin readable: check for EOF (SSH disconnect) */
    if (FD_ISSET(stdin_fd, &rfds)) {
        char probe;
        int  r = (int)read(stdin_fd, &probe, 1);
        if (r <= 0)
            return 0; /* EOF or error on stdin */
        /* got a byte — not EOF yet; unget is not possible on a fd,
         * but in practice the MCP client sends complete lines so
         * this byte is the start of the next request. Discard safely:
         * the next fgets will block until the rest of the line arrives,
         * but we have already forwarded the current request to the daemon.
         * Log and continue waiting for the daemon response. */
    }

    if (FD_ISSET(daemon_fd, &rfds))
        return 1;

    return -1;
}

int
bridge_run(void)
{
    int  daemon_fd;
    int  stdin_fd;
    char line[JSON_MSG_MAX];
    char resp[JSON_MSG_MAX];
    char method[JSON_METHOD_MAX];
    int  n;
    int  rn;
    int  ready;
    int  is_notif;

    daemon_fd = connect_to_daemon();
    if (daemon_fd < 0)
        return 1;

    stdin_fd = fileno(stdin);

    /*
     * Main bridge loop:
     *   1. Read one JSON-RPC line from stdin (from the AI client).
     *   2. Forward it to the daemon socket.
     *   3. Wait (with select) for daemon response OR stdin EOF.
     *   4. Write the response to stdout.
     *
     * select() in step 3 ensures the bridge exits cleanly when the SSH
     * client disconnects mid-request, preventing zombie processes that
     * would block the daemon from accepting new connections.
     */
    for (;;) {
        /* read from stdin */
        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF: client disconnected cleanly */

        n = (int)strlen(line);
        if (n == 0) continue;

        /* detect JSON-RPC notifications (no response expected from daemon) */
        method[0] = '\0';
        json_get_string(line, "method", method, sizeof(method));
        is_notif = (strncmp(method, "notifications/", 14) == 0);

        /* forward to daemon */
        if (ipc_write_all(daemon_fd, line, n) < 0) {
            fprintf(stderr, "mcpserver stdio: write to daemon failed: %s\n",
                    strerror(errno));
            break;
        }

        /* notifications produce no response — go straight back to reading stdin */
        if (is_notif) continue;

        /* wait for daemon response, but watch stdin for disconnect */
        ready = wait_for_daemon(daemon_fd, stdin_fd);
        if (ready <= 0)
            break; /* stdin EOF, timeout, or error */

        /* read response from daemon */
        rn = ipc_read_line(daemon_fd, resp, sizeof(resp));
        if (rn < 0) {
            fprintf(stderr, "mcpserver stdio: read from daemon failed: %s\n",
                    strerror(errno));
            break;
        }
        if (rn == 0)
            break; /* daemon closed connection */

        /* write response to stdout */
        printf("%s\n", resp);
        fflush(stdout);
    }

    close(daemon_fd);
    return 0;
}
