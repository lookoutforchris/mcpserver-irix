/*
 * protocol.c - MCP protocol handler
 *
 * STUB: dispatch logic and tool routing are not yet implemented.
 * Structure and function signatures are final.
 */

#include "protocol.h"
#include "result.h"
#include "json.h"

#include <string.h>
#include <stdio.h>
#include <syslog.h>

/* Forward declarations for tool dispatch (implemented in tools_*.c) */
/* TODO: add as tool modules are implemented */

/* ------------------------------------------------------------------ */
/* MCP method handlers                                                  */
/* ------------------------------------------------------------------ */

static int
handle_initialize(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    /*
     * MCP initialize: record client identity, respond with server
     * capabilities. In v1 we advertise tools only (no resources, no prompts).
     */
    const char *result =
        "{"
        "\"protocolVersion\":\"2024-11-05\","
        "\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{"
            "\"name\":\"irix-mcpserver\","
            "\"version\":\"" MCPSERVER_VERSION "\""
        "}"
        "}";

    json_get_string(req->params_start ? req->params_start : "{}",
                    "clientInfo.name",
                    ctx->client_name, sizeof(ctx->client_name));

    ctx->initialized = 1;
    syslog(LOG_INFO, "protocol: initialize from client \"%s\"",
           ctx->client_name);

    return jsonrpc_write_result(resp, (size_t)rsz,
                                req->id, req->id_is_null, result);
}

static int
handle_initialized(struct protocol_ctx *ctx,
                   const struct jsonrpc_request *req,
                   char *resp, int rsz)
{
    /* notifications/initialized: no response required */
    (void)ctx; (void)req; (void)resp; (void)rsz;
    return 0;
}

static int
handle_tools_list(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    /*
     * TODO: build tools array from registered tool descriptors.
     * For now returns the skeleton tool list so the protocol works.
     */
    int full = policy_is_full_profile(ctx->policy);
    const char *result_ro =
        "{\"tools\":["
        "{\"name\":\"ping\",\"description\":\"Server liveness check\","
         "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"path_exists\",\"description\":\"Check if a path exists\","
         "\"inputSchema\":{\"type\":\"object\",\"properties\":"
          "{\"path\":{\"type\":\"string\"}},"
          "\"required\":[\"path\"]}}"
        "]}";
    /* TODO: full list with write tools */
    (void)full;
    return jsonrpc_write_result(resp, (size_t)rsz,
                                req->id, req->id_is_null, result_ro);
}

static int
handle_tools_call(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    char tool[JSON_METHOD_MAX];
    const char *params = req->params_start ? req->params_start : "{}";

    if (json_get_string(params, "name", tool, sizeof(tool)) != 0) {
        return jsonrpc_write_error(resp, (size_t)rsz,
                                   req->id, req->id_is_null,
                                   JSONRPC_INVALID_PARAMS,
                                   "missing tool name");
    }

    syslog(LOG_INFO, "protocol: tools/call name=%s", tool);

    /*
     * TODO: dispatch to tool implementations:
     *   ping, path_exists, stat_path, list_directory,
     *   read_text_file, tail_text_file, search_text,
     *   read_text_around_pattern, safe_json_preview,
     *   run_inspect_command,
     *   create_text_file, replace_text_file, make_directory,
     *   delete_text_file, rename_path
     */

    return jsonrpc_write_error(resp, (size_t)rsz,
                               req->id, req->id_is_null,
                               JSONRPC_METHOD_NOT_FOUND,
                               "tool not yet implemented");
}

static int
handle_ping(struct protocol_ctx *ctx,
            const struct jsonrpc_request *req,
            char *resp, int rsz)
{
    (void)ctx;
    return jsonrpc_write_result(resp, (size_t)rsz,
                                req->id, req->id_is_null, "{}");
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
protocol_init(struct protocol_ctx *ctx, const struct policy *policy)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->policy = policy;
    ctx->initialized = 0;
}

int
protocol_handle(struct protocol_ctx *ctx,
                const struct jsonrpc_request *req,
                char *resp_buf, int resp_bufsz)
{
    const char *m = req->method;

    if (strcmp(m, "initialize") == 0)
        return handle_initialize(ctx, req, resp_buf, resp_bufsz);

    if (strcmp(m, "notifications/initialized") == 0)
        return handle_initialized(ctx, req, resp_buf, resp_bufsz);

    if (strcmp(m, "ping") == 0)
        return handle_ping(ctx, req, resp_buf, resp_bufsz);

    if (!ctx->initialized) {
        return jsonrpc_write_error(resp_buf, (size_t)resp_bufsz,
                                   req->id, req->id_is_null,
                                   JSONRPC_INVALID_REQUEST,
                                   "not initialized");
    }

    if (strcmp(m, "tools/list") == 0)
        return handle_tools_list(ctx, req, resp_buf, resp_bufsz);

    if (strcmp(m, "tools/call") == 0)
        return handle_tools_call(ctx, req, resp_buf, resp_bufsz);

    return jsonrpc_write_error(resp_buf, (size_t)resp_bufsz,
                               req->id, req->id_is_null,
                               JSONRPC_METHOD_NOT_FOUND,
                               "method not found");
}
