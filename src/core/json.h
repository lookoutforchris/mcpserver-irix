/*
 * json.h - minimal JSON-RPC 2.0 message handling
 *
 * Provides just enough JSON parsing and serialisation to implement the MCP
 * stdio transport: parse an incoming newline-delimited JSON-RPC request,
 * extract method/id/params, and write result or error responses.
 *
 * This is not a general-purpose JSON library. It handles well-formed
 * JSON-RPC 2.0 messages as produced by compliant MCP clients.
 */

#ifndef MCPSERVER_JSON_H
#define MCPSERVER_JSON_H

#include <stddef.h>          /* size_t */
#include "../compat/compat.h"

/* Buffer sizes */
#define JSON_ID_MAX      64      /* max chars in a request id */
#define JSON_METHOD_MAX  64      /* max chars in a method name */
#define JSON_MSG_MAX     65536   /* max incoming message size (64 KiB) */

/*
 * A parsed JSON-RPC 2.0 request.
 * params_start points into buf at the raw JSON value for "params".
 */
struct jsonrpc_request {
    char    id[JSON_ID_MAX];        /* request id as a string */
    int     id_is_null;             /* 1 if id was JSON null (notification) */
    char    method[JSON_METHOD_MAX];
    char   *params_start;           /* pointer into buf, or NULL */
    char    buf[JSON_MSG_MAX];      /* raw message (null-terminated) */
};

/* Standard JSON-RPC error codes */
#define JSONRPC_PARSE_ERROR      -32700
#define JSONRPC_INVALID_REQUEST  -32600
#define JSONRPC_METHOD_NOT_FOUND -32601
#define JSONRPC_INVALID_PARAMS   -32602
#define JSONRPC_INTERNAL_ERROR   -32603

/*
 * jsonrpc_parse - parse one line of JSON-RPC into req.
 *
 * Copies line into req->buf and sets req->id, req->method, req->params_start.
 * Returns 0 on success, -1 on parse error.
 */
int jsonrpc_parse(const char *line, struct jsonrpc_request *req);

/*
 * jsonrpc_write_result - write a successful JSON-RPC response.
 *
 * result_json is the raw JSON value for the "result" field (may be an
 * object, array, or primitive). Written to buf up to bufsz bytes.
 * Returns bytes written (not including null terminator), -1 on error.
 */
int jsonrpc_write_result(char *buf, size_t bufsz,
                         const char *id, int id_is_null,
                         const char *result_json);

/*
 * jsonrpc_write_error - write a JSON-RPC error response.
 */
int jsonrpc_write_error(char *buf, size_t bufsz,
                        const char *id, int id_is_null,
                        int code, const char *message);

/*
 * json_escape - escape a C string for embedding inside a JSON string literal.
 * Escapes \, ", and control characters. Writes to out up to outsz-1 bytes.
 * Returns number of bytes written (not including null terminator).
 */
int json_escape(const char *in, char *out, size_t outsz);

/*
 * json_get_string - extract a string field from a flat JSON object.
 * Writes the unescaped value into out (up to outsz-1 bytes).
 * Returns 0 on success, -1 if field not found or not a string.
 *
 * Note: only handles top-level string fields in simple objects.
 */
int json_get_string(const char *json, const char *field,
                    char *out, size_t outsz);

/*
 * json_get_int - extract an integer field from a flat JSON object.
 * Returns 0 on success, -1 if field not found or not an integer.
 */
int json_get_int(const char *json, const char *field, int *out);

/*
 * json_get_bool - extract a boolean field (true/false) from a JSON object.
 * Sets *out to 1 (true) or 0 (false).
 * Returns 0 on success, -1 if not found or not a boolean.
 */
int json_get_bool(const char *json, const char *field, int *out);

#endif /* MCPSERVER_JSON_H */
