/*
 * stdio_bridge.c - stdio <-> UNIX socket MCP bridge
 *
 * Connects to /var/run/mcpserverd.sock and proxies JSON-RPC lines
 * between stdin/stdout and the daemon socket.
 */

#include "stdio_bridge.h"
#include "../daemon/ipc.h"
#include "../core/json.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
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

int
bridge_run(void)
{
    int  daemon_fd;
    char line[JSON_MSG_MAX];
    char resp[JSON_MSG_MAX];
    int  n;
    int  rn;

    daemon_fd = connect_to_daemon();
    if (daemon_fd < 0)
        return 1;

    /*
     * Main bridge loop:
     *   1. Read one JSON-RPC line from stdin (from the AI client).
     *   2. Forward it to the daemon socket.
     *   3. Read one response line from the daemon.
     *   4. Write the response to stdout.
     *
     * v1: strictly synchronous request/response pairs. This matches
     * the MCP stdio transport model where each request gets one response
     * before the next request is sent.
     */
    for (;;) {
        /* read from stdin */
        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF: client disconnected */

        n = (int)strlen(line);
        if (n == 0) continue;

        /* forward to daemon */
        if (ipc_write_all(daemon_fd, line, n) < 0) {
            fprintf(stderr, "mcpserver stdio: write to daemon failed: %s\n",
                    strerror(errno));
            break;
        }

        /* read response from daemon */
        rn = ipc_read_line(daemon_fd, resp, sizeof(resp));
        if (rn < 0) {
            fprintf(stderr, "mcpserver stdio: read from daemon failed: %s\n",
                    strerror(errno));
            break;
        }
        if (rn == 0)
            break; /* daemon closed connection */

        /* write response to stdout (add newline if stripped by read_line) */
        printf("%s\n", resp);
        fflush(stdout);
    }

    close(daemon_fd);
    return 0;
}
