/*
 * ipc.h - UNIX domain socket abstraction for the daemon
 *
 * Isolates all socket file management so the rest of the daemon does not
 * need to know the socket path or deal with stale socket cleanup.
 *
 * Per docs/ARCHITECTURE.md §3:
 *   - Socket at /var/run/mcpserverd.sock
 *   - AF_UNIX, SOCK_STREAM
 *   - Mode 0600 (root only)
 *   - unlink() before bind() to clear stale sockets
 */

#ifndef MCPSERVER_IPC_H
#define MCPSERVER_IPC_H

#define IPC_SOCK_PATH    "/var/run/mcpserverd.sock"
#define IPC_LISTEN_BACKLOG  8

/*
 * ipc_listen - create, bind, and listen on the UNIX domain socket.
 *
 * Unlinks any existing socket file first. Sets socket mode to 0600.
 * Returns the listening socket fd on success, -1 on error.
 */
int ipc_listen(void);

/*
 * ipc_accept - accept one incoming connection.
 * Returns the connected client fd, -1 on error (check errno for EINTR).
 */
int ipc_accept(int listen_fd);

/*
 * ipc_close_listener - close the listening socket and unlink the socket file.
 * Called at clean shutdown.
 */
void ipc_close_listener(int listen_fd);

/*
 * ipc_read_line - read one newline-terminated line from fd into buf.
 * Returns bytes read (not including newline), 0 on EOF, -1 on error.
 */
int ipc_read_line(int fd, char *buf, int bufsz);

/*
 * ipc_write_all - write all n bytes of buf to fd.
 * Retries on EINTR. Returns 0 on success, -1 on error.
 */
int ipc_write_all(int fd, const char *buf, int n);

#endif /* MCPSERVER_IPC_H */
