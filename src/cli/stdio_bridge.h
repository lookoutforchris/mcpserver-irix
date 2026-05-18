/*
 * stdio_bridge.h - stdio <-> UNIX socket MCP bridge
 *
 * Implements "mcpserver stdio": reads JSON-RPC lines from stdin (from the
 * AI client), forwards them to mcpserverd via the UNIX domain socket,
 * reads responses, and writes them to stdout.
 *
 * Diagnostics go to stderr only. Stdout is reserved for MCP traffic.
 * Exits cleanly when stdin reaches EOF (AI client disconnected).
 */

#ifndef MCPSERVER_STDIO_BRIDGE_H
#define MCPSERVER_STDIO_BRIDGE_H

/*
 * bridge_run - run the stdio bridge until EOF on stdin or socket error.
 * Returns 0 on clean exit, 1 on error.
 */
int bridge_run(void);

#endif /* MCPSERVER_STDIO_BRIDGE_H */
