/*
 * ipc.c - UNIX domain socket abstraction
 *
 * Implements the IRIX-specific socket management for mcpserverd.
 *
 * Key IRIX notes (from IRIX Network Programming Guide 007-0810-110):
 *   - AF_UNIX socket creates a real filesystem file; must unlink before bind.
 *   - bind() length = strlen(sun_path) + sizeof(sun_family) (no null byte).
 *   - Use FNDELAY for non-blocking, not O_NONBLOCK.
 *   - EWOULDBLOCK == EAGAIN on IRIX; handle both.
 */

#include "ipc.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>

int
ipc_listen(void)
{
    int                fd;
    struct sockaddr_un addr;

    /* unlink stale socket from previous run or crash */
    unlink(IPC_SOCK_PATH);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        syslog(LOG_ERR, "ipc_listen: socket: %m");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (strlen(IPC_SOCK_PATH) >= sizeof(addr.sun_path)) {
        syslog(LOG_ERR, "ipc_listen: socket path too long");
        close(fd);
        return -1;
    }
    strcpy(addr.sun_path, IPC_SOCK_PATH);

    /*
     * IRIX bind() length: strlen(sun_path) + sizeof(sun_family).
     * Null bytes are not counted per IRIX Network Programming Guide.
     */
    if (bind(fd, (struct sockaddr *)&addr,
             (int)(strlen(addr.sun_path) + sizeof(addr.sun_family))) < 0) {
        syslog(LOG_ERR, "ipc_listen: bind %s: %m", IPC_SOCK_PATH);
        close(fd);
        return -1;
    }

    /* restrict to root only */
    if (chmod(IPC_SOCK_PATH, 0600) < 0)
        syslog(LOG_WARNING, "ipc_listen: chmod: %m");

    if (listen(fd, IPC_LISTEN_BACKLOG) < 0) {
        syslog(LOG_ERR, "ipc_listen: listen: %m");
        close(fd);
        unlink(IPC_SOCK_PATH);
        return -1;
    }

    syslog(LOG_INFO, "ipc: listening on %s", IPC_SOCK_PATH);
    return fd;
}

int
ipc_accept(int listen_fd)
{
    struct sockaddr_un client;
    int                clen;
    int                fd;

    clen = (int)sizeof(client);
    fd = accept(listen_fd, (struct sockaddr *)&client, &clen);
    if (fd < 0) {
        if (errno != EINTR)
            syslog(LOG_ERR, "ipc_accept: %m");
        return -1;
    }
    syslog(LOG_INFO, "ipc: client connected");
    return fd;
}

void
ipc_close_listener(int listen_fd)
{
    if (listen_fd >= 0)
        close(listen_fd);
    unlink(IPC_SOCK_PATH);
    syslog(LOG_INFO, "ipc: listener closed");
}

int
ipc_read_line(int fd, char *buf, int bufsz)
{
    int   n   = 0;
    char  c;
    int   ret;

    while (n < bufsz - 1) {
        do {
            ret = (int)read(fd, &c, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) return -1;
        if (ret == 0) return 0; /* EOF */

        if (c == '\n') {
            buf[n] = '\0';
            return n;
        }
        buf[n++] = c;
    }
    buf[n] = '\0';
    return n; /* line was truncated; caller should handle */
}

int
ipc_write_all(int fd, const char *buf, int n)
{
    int written = 0;
    int ret;

    while (written < n) {
        do {
            ret = (int)write(fd, buf + written, (size_t)(n - written));
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) return -1;
        written += ret;
    }
    return 0;
}
