/*
 * tools_fs.c - filesystem read and inspect tools
 *
 * STUB: function bodies are not yet implemented.
 * Signatures, response shape, and authorization flow are final.
 */

#include "tools_fs.h"
#include "result.h"
#include "json.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int
tool_ping(const struct policy *p, char *resp_buf, int resp_bufsz)
{
    char result[512];
    snprintf(result, sizeof(result),
             "{\"ok\":true,"
             "\"server\":\"irix-mcpserver\","
             "\"version\":\"%s\","
             "\"profile\":\"%s\"}",
             MCPSERVER_VERSION,
             policy_is_full_profile(p) ? "full" : "readonly");
    return snprintf(resp_buf, (size_t)resp_bufsz, "%s", result);
}

int
tool_path_exists(const struct policy *p, const char *path,
                 char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    struct stat st;
    int         allowed, exists;

    allowed = policy_is_read_allowed(p, path);
    json_escape(path, epath, sizeof(epath));

    if (!allowed) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\"}", epath);
    }

    mcp_realpath(path, canonical);
    exists = (stat(canonical, &st) == 0) ? 1 : 0;

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":%s,\"path\":\"%s\"}",
                    exists ? "true" : "false", epath);
}

int
tool_stat_path(const struct policy *p, const char *path,
               char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    struct stat st;
    const char *kind;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"kind\":null,"
                        "\"size_bytes\":null}", epath);
    }

    mcp_realpath(path, canonical);

    if (stat(canonical, &st) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":false,"
                        "\"path\":\"%s\",\"kind\":null,"
                        "\"size_bytes\":null}", epath);
    }

    if (S_ISREG(st.st_mode))       kind = "file";
    else if (S_ISDIR(st.st_mode))  kind = "directory";
    else                            kind = "other";

    if (S_ISREG(st.st_mode)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":true,"
                        "\"path\":\"%s\",\"kind\":\"%s\","
                        "\"size_bytes\":%ld}",
                        epath, kind, (long)st.st_size);
    }
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":true,"
                    "\"path\":\"%s\",\"kind\":\"%s\","
                    "\"size_bytes\":null}",
                    epath, kind);
}

int
tool_list_directory(const struct policy *p, const char *path,
                    char *resp_buf, int resp_bufsz)
{
    /* TODO: implement directory listing with deny filtering */
    (void)p; (void)path;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"exists\":false,"
                    "\"path\":\"\",\"entries\":[],"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_read_text_file(const struct policy *p, const char *path,
                    int start_line, int max_lines,
                    char *resp_buf, int resp_bufsz)
{
    /* TODO: implement line-range file reading */
    (void)p; (void)path; (void)start_line; (void)max_lines;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"exists\":false,"
                    "\"path\":\"\",\"content\":\"\","
                    "\"start_line\":1,\"lines_returned\":0,"
                    "\"truncated\":false,"
                    "\"error\":\"not yet implemented\"}");
}

int
tool_tail_text_file(const struct policy *p, const char *path,
                    int n_lines,
                    char *resp_buf, int resp_bufsz)
{
    /* TODO: implement tail (read last n lines) */
    (void)p; (void)path; (void)n_lines;
    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":false,\"exists\":false,"
                    "\"path\":\"\",\"content\":\"\","
                    "\"start_line\":1,\"lines_returned\":0,"
                    "\"truncated\":false,"
                    "\"error\":\"not yet implemented\"}");
}
