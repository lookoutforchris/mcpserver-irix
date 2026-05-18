/*
 * tools_text.c - text search and pattern context tools
 *
 * STUB: not yet implemented.
 */

#include "tools_text.h"
#include "result.h"
#include "json.h"

#include <stdio.h>
#include <string.h>

int
tool_search_text(const struct policy *p,
                 const char *root_path,
                 const char *pattern,
                 const char **include_globs,
                 int max_results,
                 int case_sensitive,
                 char *resp_buf, int resp_bufsz)
{
    (void)p; (void)root_path; (void)pattern;
    (void)include_globs; (void)max_results; (void)case_sensitive;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"root_path\":\"\","
                    "\"pattern\":\"\",\"matches\":[],"
                    "\"truncated\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_read_text_around_pattern(const struct policy *p,
                              const char *path,
                              const char *pattern,
                              int context_before,
                              int context_after,
                              int match_index,
                              int case_sensitive,
                              char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path; (void)pattern;
    (void)context_before; (void)context_after;
    (void)match_index; (void)case_sensitive;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"exists\":false,"
                    "\"path\":\"\",\"found\":false,"
                    "\"match_line\":0,\"content\":\"\","
                    "\"error\":\"not yet implemented\"}");
}

int
tool_safe_json_preview(const struct policy *p,
                       const char *path,
                       int top_level_only,
                       int max_bytes,
                       char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path; (void)top_level_only; (void)max_bytes;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"exists\":false,"
                    "\"path\":\"\",\"content\":\"\","
                    "\"truncated\":false,"
                    "\"error\":\"not yet implemented\"}");
}
