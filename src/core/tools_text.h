/*
 * tools_text.h - text search and pattern context tools
 *
 * Implements: search_text, read_text_around_pattern, safe_json_preview
 */

#ifndef MCPSERVER_TOOLS_TEXT_H
#define MCPSERVER_TOOLS_TEXT_H

#include "policy.h"

/*
 * tool_search_text - recursive substring search under root_path.
 *
 * include_globs: NULL-terminated array of glob strings, e.g. {"*.c","*.h",NULL}
 *                Pass NULL for all files.
 * max_results: capped at MCP_SEARCH_MAX.
 * case_sensitive: 1 for case-sensitive, 0 for case-insensitive.
 */
int tool_search_text(const struct policy *p,
                     const char *root_path,
                     const char *pattern,
                     const char **include_globs,
                     int max_results,
                     int case_sensitive,
                     char *resp_buf, int resp_bufsz);

/*
 * tool_read_text_around_pattern - return context lines around Nth match.
 *
 * match_index: 1-based occurrence number.
 */
int tool_read_text_around_pattern(const struct policy *p,
                                  const char *path,
                                  const char *pattern,
                                  int context_before,
                                  int context_after,
                                  int match_index,
                                  int case_sensitive,
                                  char *resp_buf, int resp_bufsz);

/*
 * tool_safe_json_preview - preview JSON structure of a file.
 *
 * top_level_only: 1 to summarise nested values rather than expand them.
 * max_bytes: clamp file read at this many bytes.
 */
int tool_safe_json_preview(const struct policy *p,
                           const char *path,
                           int top_level_only,
                           int max_bytes,
                           char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_TOOLS_TEXT_H */
