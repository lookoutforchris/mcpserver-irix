/*
 * protocol.c - MCP protocol handler
 *
 * Handles MCP lifecycle and dispatches tools/list and tools/call to the
 * tool implementations in tools_fs.c, tools_text.c, tools_write.c,
 * tools_exec.c.
 */

#include "protocol.h"
#include "result.h"
#include "json.h"
#include "tools_fs.h"
#include "tools_text.h"
#include "tools_write.h"
#include "tools_exec.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

/* ------------------------------------------------------------------ */
/* tools/list response                                                  */
/* ------------------------------------------------------------------ */

/*
 * TOOLS_LIST_RO - tools available in readonly profile.
 * Broken across adjacent string literals for readability (C89 legal).
 */
static const char TOOLS_LIST_RO[] =
    "{\"tools\":["
    "{\"name\":\"ping\","
     "\"description\":\"Server liveness and identity check.\","
     "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

    "{\"name\":\"path_exists\","
     "\"description\":\"Check if a path is within an allowed root and exists.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"stat_path\","
     "\"description\":\"Return kind (file/directory/other) and size of a path.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"list_directory\","
     "\"description\":\"List immediate children of a directory.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"read_text_file\","
     "\"description\":\"Read a slice of a text file (start_line..start_line+max_lines).\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"start_line\":{\"type\":\"integer\",\"default\":1},"
       "\"max_lines\":{\"type\":\"integer\",\"default\":200}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"tail_text_file\","
     "\"description\":\"Return the last N lines of a text file.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"lines\":{\"type\":\"integer\",\"default\":100}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"search_text\","
     "\"description\":\"Recursive substring search under a root path.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"root_path\":{\"type\":\"string\"},"
       "\"pattern\":{\"type\":\"string\"},"
       "\"include_globs\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
       "\"max_results\":{\"type\":\"integer\",\"default\":50},"
       "\"case_sensitive\":{\"type\":\"boolean\",\"default\":true}},"
      "\"required\":[\"root_path\",\"pattern\"]}},"

    "{\"name\":\"read_text_around_pattern\","
     "\"description\":\"Return context lines around the Nth occurrence of a pattern.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"pattern\":{\"type\":\"string\"},"
       "\"context_before\":{\"type\":\"integer\",\"default\":10},"
       "\"context_after\":{\"type\":\"integer\",\"default\":20},"
       "\"match_index\":{\"type\":\"integer\",\"default\":1},"
       "\"case_sensitive\":{\"type\":\"boolean\",\"default\":true}},"
      "\"required\":[\"path\",\"pattern\"]}},"

    "{\"name\":\"safe_json_preview\","
     "\"description\":\"Preview the structure of a JSON file.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"top_level_only\":{\"type\":\"boolean\",\"default\":true},"
       "\"max_bytes\":{\"type\":\"integer\",\"default\":12000}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"run_inspect_command\","
     "\"description\":\"Execute one whitelisted read-only command (full profile only).\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"command\":{\"type\":\"string\"},"
       "\"args\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
      "\"required\":[\"command\",\"args\"]}}"
    "]}";

/* Write tools appended for full profile - built dynamically in handle_tools_list */
static const char WRITE_TOOLS[] =
    ",{\"name\":\"create_text_file\","
     "\"description\":\"Create a new text file within a read-write root.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"content\":{\"type\":\"string\"}},"
      "\"required\":[\"path\",\"content\"]}},"

    "{\"name\":\"replace_text_file\","
     "\"description\":\"Overwrite an existing text file within a read-write root.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"path\":{\"type\":\"string\"},"
       "\"content\":{\"type\":\"string\"}},"
      "\"required\":[\"path\",\"content\"]}},"

    "{\"name\":\"make_directory\","
     "\"description\":\"Create a single directory within a read-write root.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"delete_text_file\","
     "\"description\":\"Delete a text file within a read-write root.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}},"

    "{\"name\":\"rename_path\","
     "\"description\":\"Rename a file or directory within the same read-write root.\","
     "\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{"
       "\"source_path\":{\"type\":\"string\"},"
       "\"dest_path\":{\"type\":\"string\"}},"
      "\"required\":[\"source_path\",\"dest_path\"]}}";

/* ------------------------------------------------------------------ */
/* MCP method handlers                                                  */
/* ------------------------------------------------------------------ */

static int
handle_initialize(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    const char *result =
        "{"
        "\"protocolVersion\":\"2024-11-05\","
        "\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{"
            "\"name\":\"irix-mcpserver\","
            "\"version\":\"" MCPSERVER_VERSION "\""
        "}"
        "}";

    if (req->params_start)
        json_get_string(req->params_start, "clientInfo.name",
                        ctx->client_name, sizeof(ctx->client_name));

    ctx->initialized = 1;
    syslog(LOG_INFO, "protocol: initialize from \"%s\"", ctx->client_name);

    return jsonrpc_write_result(resp, (size_t)rsz,
                                req->id, req->id_is_null, result);
}

static int
handle_tools_list(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    static char tools_json[32768];
    int         full = policy_is_full_profile(ctx->policy);
    int         n;

    if (!full) {
        /* readonly: return the base list as-is */
        return jsonrpc_write_result(resp, (size_t)rsz,
                                    req->id, req->id_is_null, TOOLS_LIST_RO);
    }

    /* full profile: splice write tools in before the closing ]} */
    n = (int)strlen(TOOLS_LIST_RO);
    /* TOOLS_LIST_RO ends with "]}"; we insert WRITE_TOOLS before "}" */
    memcpy(tools_json, TOOLS_LIST_RO, (size_t)(n - 2)); /* everything up to ]} */
    tools_json[n - 2] = '\0';
    strcat(tools_json, WRITE_TOOLS);
    strcat(tools_json, "]}");

    return jsonrpc_write_result(resp, (size_t)rsz,
                                req->id, req->id_is_null, tools_json);
}

/*
 * wrap_tool_result - wrap a tool result JSON object in MCP tools/call format.
 * MCP wraps tool results in {"content":[{"type":"text","text":"..."}]}.
 * Returns bytes written.
 */
static int
wrap_tool_result(char *resp, int rsz, const char *id, int id_is_null,
                 const char *tool_json)
{
    static char result_json[PROTO_RESP_MAX];
    static char escaped[PROTO_RESP_MAX];
    int         n;

    json_escape(tool_json, escaped, sizeof(escaped));
    n = snprintf(result_json, sizeof(result_json),
                 "{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}]}",
                 escaped);
    if (n <= 0) return -1;

    return jsonrpc_write_result(resp, (size_t)rsz, id, id_is_null, result_json);
}

static int
handle_tools_call(struct protocol_ctx *ctx,
                  const struct jsonrpc_request *req,
                  char *resp, int rsz)
{
    char        tool[JSON_METHOD_MAX];
    char        args_json[16384];   /* raw "arguments" sub-object */
    char        path[MCPSERVER_PATH_MAX];
    char        path2[MCPSERVER_PATH_MAX];
    char        content_buf[MCP_CONTENT_MAX + 1];
    static char result[PROTO_RESP_MAX];
    const char *params;
    int         int_val;
    int         bool_val;
    int         n;

    params = req->params_start ? req->params_start : "{}";

    if (json_get_string(params, "name", tool, sizeof(tool)) != 0) {
        return jsonrpc_write_error(resp, (size_t)rsz,
                                   req->id, req->id_is_null,
                                   JSONRPC_INVALID_PARAMS,
                                   "missing tool name");
    }

    syslog(LOG_INFO, "tools/call: %s", tool);

    /* extract "arguments" sub-object (may be absent for no-arg tools) */
    args_json[0] = '\0';
    json_get_object(params, "arguments", args_json, sizeof(args_json));

    /* ---- read-only tools ---- */

    if (strcmp(tool, "ping") == 0) {
        n = tool_ping(ctx->policy, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "path_exists") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        n = tool_path_exists(ctx->policy, path, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "stat_path") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        n = tool_stat_path(ctx->policy, path, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "list_directory") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        n = tool_list_directory(ctx->policy, path, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "read_text_file") == 0) {
        int start = 1, maxl = MCP_DEFAULT_READ_LINES;
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        json_get_int(args_json, "start_line", &start);
        json_get_int(args_json, "max_lines",  &maxl);
        n = tool_read_text_file(ctx->policy, path, start, maxl,
                                result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "tail_text_file") == 0) {
        int lines = MCP_DEFAULT_TAIL_LINES;
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        json_get_int(args_json, "lines", &lines);
        n = tool_tail_text_file(ctx->policy, path, lines,
                                result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "search_text") == 0) {
        int max = MCP_DEFAULT_SEARCH_MAX, cs = 1;
        if (json_get_string(args_json, "root_path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing root_path");
        if (json_get_string(args_json, "pattern", path2, sizeof(path2)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing pattern");
        json_get_int(args_json, "max_results", &max);
        json_get_bool(args_json, "case_sensitive", &cs);
        n = tool_search_text(ctx->policy, path, path2, NULL,
                             max, cs, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "read_text_around_pattern") == 0) {
        int cb = MCP_DEFAULT_CTX_BEFORE, ca = MCP_DEFAULT_CTX_AFTER;
        int mi = 1, cs = 1;
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        if (json_get_string(args_json, "pattern", path2, sizeof(path2)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing pattern");
        json_get_int(args_json, "context_before", &cb);
        json_get_int(args_json, "context_after",  &ca);
        json_get_int(args_json, "match_index",    &mi);
        json_get_bool(args_json, "case_sensitive", &cs);
        n = tool_read_text_around_pattern(ctx->policy, path, path2,
                                          cb, ca, mi, cs,
                                          result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "safe_json_preview") == 0) {
        int tlo = 1, mb = MCP_DEFAULT_JSON_BYTES;
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        json_get_bool(args_json, "top_level_only", &tlo);
        json_get_int(args_json,  "max_bytes", &mb);
        n = tool_safe_json_preview(ctx->policy, path, tlo, mb,
                                   result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "run_inspect_command") == 0) {
        char cmd[32];
        char args_raw[1024];
        const char *arg_ptrs[EXEC_ARGS_MAX + 1];
        /* TODO: parse args array from args_json */
        (void)args_raw; (void)arg_ptrs; (void)content_buf;
        if (json_get_string(args_json, "command", cmd, sizeof(cmd)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing command");
        arg_ptrs[0] = NULL;
        n = tool_run_inspect_command(ctx->policy, cmd,
                                     arg_ptrs, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    /* ---- write tools (full profile only) ---- */

    if (!policy_is_full_profile(ctx->policy)) {
        return jsonrpc_write_error(resp, (size_t)rsz,
                                   req->id, req->id_is_null,
                                   JSONRPC_METHOD_NOT_FOUND,
                                   "tool not available in readonly profile");
    }

    if (strcmp(tool, "create_text_file") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0 ||
            json_get_string(args_json, "content", content_buf,
                            sizeof(content_buf)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path or content");
        n = tool_create_text_file(ctx->policy, path, content_buf,
                                  result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "replace_text_file") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0 ||
            json_get_string(args_json, "content", content_buf,
                            sizeof(content_buf)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path or content");
        n = tool_replace_text_file(ctx->policy, path, content_buf,
                                   result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "make_directory") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        n = tool_make_directory(ctx->policy, path, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "delete_text_file") == 0) {
        if (json_get_string(args_json, "path", path, sizeof(path)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing path");
        n = tool_delete_text_file(ctx->policy, path, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    if (strcmp(tool, "rename_path") == 0) {
        if (json_get_string(args_json, "source_path", path,  sizeof(path))  != 0 ||
            json_get_string(args_json, "dest_path",   path2, sizeof(path2)) != 0)
            return jsonrpc_write_error(resp, (size_t)rsz,
                       req->id, req->id_is_null,
                       JSONRPC_INVALID_PARAMS, "missing source_path or dest_path");
        n = tool_rename_path(ctx->policy, path, path2, result, sizeof(result));
        return (n > 0) ? wrap_tool_result(resp, rsz,
                         req->id, req->id_is_null, result) : -1;
    }

    /* suppress unused variable warnings for stubs */
    (void)int_val; (void)bool_val;

    return jsonrpc_write_error(resp, (size_t)rsz,
                               req->id, req->id_is_null,
                               JSONRPC_METHOD_NOT_FOUND,
                               "unknown tool");
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
        return 0; /* notification: no response */

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
