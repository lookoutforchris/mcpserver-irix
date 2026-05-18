/*
 * result.h - shared return codes and buffer size constants
 *
 * All tool functions return one of these codes. The JSON serialisation
 * layer maps them to the appropriate response fields (allowed, exists,
 * error, etc.) as specified in docs/TOOL_CONTRACT.md.
 */

#ifndef MCPSERVER_RESULT_H
#define MCPSERVER_RESULT_H

/* Return codes */
#define MCP_OK            0   /* success */
#define MCP_ERR_DENIED    1   /* path outside allowed roots or matches deny */
#define MCP_ERR_NOENT     2   /* path allowed but does not exist */
#define MCP_ERR_IO        3   /* I/O or syscall error */
#define MCP_ERR_POLICY    4   /* policy violation (extension, command, etc.) */
#define MCP_ERR_ARGS      5   /* bad or missing arguments */
#define MCP_ERR_NOMEM     6   /* allocation failure */
#define MCP_ERR_TRUNC     7   /* output was truncated (non-fatal) */
#define MCP_ERR_NOTFOUND  8   /* pattern or item not found */
#define MCP_ERR_BINARY    9   /* file is not valid text */
#define MCP_ERR_TOOBIG    10  /* request exceeds allowed size */
#define MCP_ERR_TIMEOUT   11  /* command execution timed out */

/* Output size limits (in bytes / chars) */
#define MCP_CONTENT_MAX    20000  /* max chars in any content/output field */
#define MCP_LIST_MAX       500    /* max entries from list_directory */
#define MCP_SEARCH_MAX     200    /* max matches from search_text */
#define MCP_LINE_MAX       4096   /* max length of a single line */

/* Default parameter values */
#define MCP_DEFAULT_READ_LINES   200
#define MCP_DEFAULT_TAIL_LINES   100
#define MCP_DEFAULT_SEARCH_MAX   50
#define MCP_DEFAULT_JSON_BYTES   12000
#define MCP_DEFAULT_CTX_BEFORE   10
#define MCP_DEFAULT_CTX_AFTER    20
#define MCP_CMD_TIMEOUT_SEC      30

#endif /* MCPSERVER_RESULT_H */
