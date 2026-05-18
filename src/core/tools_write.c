/*
 * tools_write.c - file write and path management tools
 *
 * STUB: not yet implemented.
 */

#include "tools_write.h"
#include "result.h"
#include "json.h"

#include <stdio.h>
#include <string.h>

int
tool_create_text_file(const struct policy *p,
                      const char *path, const char *content,
                      char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path; (void)content;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"path\":\"\","
                    "\"created\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_replace_text_file(const struct policy *p,
                       const char *path, const char *content,
                       char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path; (void)content;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"path\":\"\","
                    "\"replaced\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_make_directory(const struct policy *p, const char *path,
                    char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"path\":\"\","
                    "\"created\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_delete_text_file(const struct policy *p, const char *path,
                      char *resp_buf, int resp_bufsz)
{
    (void)p; (void)path;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"path\":\"\","
                    "\"deleted\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_rename_path(const struct policy *p,
                 const char *source, const char *dest,
                 char *resp_buf, int resp_bufsz)
{
    (void)p; (void)source; (void)dest;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"source\":\"\",\"dest\":\"\","
                    "\"renamed\":false,"
                    "\"error\":\"not yet implemented\"}");
}
