/*
 * tools_fs.h - filesystem read and inspect tools
 *
 * Implements: ping, path_exists, stat_path, list_directory,
 *             read_text_file, tail_text_file
 *
 * All functions write a JSON result object into resp_buf and return the
 * number of bytes written, or -1 on internal error.
 * Authorization must be checked by the caller before invoking these.
 */

#ifndef MCPSERVER_TOOLS_FS_H
#define MCPSERVER_TOOLS_FS_H

#include "policy.h"

/*
 * tool_ping - return server identity and liveness.
 */
int tool_ping(const struct policy *p,
              char *resp_buf, int resp_bufsz);

/*
 * tool_path_exists - check if a path is allowed and exists.
 */
int tool_path_exists(const struct policy *p, const char *path,
                     char *resp_buf, int resp_bufsz);

/*
 * tool_stat_path - return kind and size of a path.
 */
int tool_stat_path(const struct policy *p, const char *path,
                   char *resp_buf, int resp_bufsz);

/*
 * tool_list_directory - list immediate children of a directory.
 * Entries matching deny rules are silently omitted.
 */
int tool_list_directory(const struct policy *p, const char *path,
                        char *resp_buf, int resp_bufsz);

/*
 * tool_read_text_file - read start_line..start_line+max_lines-1 of a file.
 * start_line is 1-based. max_lines capped at MCP_DEFAULT_READ_LINES.
 */
int tool_read_text_file(const struct policy *p, const char *path,
                        int start_line, int max_lines,
                        char *resp_buf, int resp_bufsz);

/*
 * tool_tail_text_file - read the last n_lines lines of a file.
 */
int tool_tail_text_file(const struct policy *p, const char *path,
                        int n_lines,
                        char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_TOOLS_FS_H */
