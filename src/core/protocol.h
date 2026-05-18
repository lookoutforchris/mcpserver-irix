/*
 * protocol.h - MCP protocol handler
 *
 * Handles MCP lifecycle (initialize, initialized, ping) and dispatches
 * tools/list and tools/call requests to the appropriate tool functions.
 *
 * See docs/TOOL_CONTRACT.md for tool specifications.
 * See docs/ARCHITECTURE.md §3 for the wire protocol.
 */

#ifndef MCPSERVER_PROTOCOL_H
#define MCPSERVER_PROTOCOL_H

#include "json.h"
#include "policy.h"

/* Response buffer size for a single MCP response */
#define PROTO_RESP_MAX  131072  /* 128 KiB */

/*
 * protocol_ctx - per-connection protocol state.
 */
struct protocol_ctx {
    const struct policy *policy;   /* loaded boundary policy */
    int initialized;               /* 1 after MCP initialize handshake */
    char client_name[64];          /* from initialize params */
    char client_version[32];
};

/*
 * protocol_init - initialise a protocol context.
 */
void protocol_init(struct protocol_ctx *ctx, const struct policy *policy);

/*
 * protocol_handle - process one JSON-RPC request and write a response.
 *
 * Reads req, dispatches to the correct handler, and writes a complete
 * newline-terminated JSON-RPC response into resp_buf (up to resp_bufsz bytes).
 *
 * Returns the number of bytes written to resp_buf, or -1 on error.
 */
int protocol_handle(struct protocol_ctx *ctx,
                    const struct jsonrpc_request *req,
                    char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_PROTOCOL_H */
