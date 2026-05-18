/*
 * tools_write.c - file write and path management tools
 *
 * Implements: create_text_file, replace_text_file, make_directory,
 *             delete_text_file, rename_path
 *
 * All operations require full profile and write-allowed path.
 */

#include "tools_write.h"
#include "result.h"
#include "json.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * write_file - write content to a file, creating or truncating.
 * flags: O_CREAT|O_EXCL for create (fail if exists),
 *        O_TRUNC for replace (fail if not exists checked by caller).
 * Returns 0 on success, -1 on error (errno set).
 */
static int
write_file(const char *path, const char *content, int flags)
{
    int    fd;
    size_t len;
    int    n;

    fd = open(path, flags | O_WRONLY, 0644);
    if (fd < 0) return -1;

    len = strlen(content);
    while (len > 0) {
        n = (int)write(fd, content, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        content += n;
        len     -= (size_t)n;
    }
    close(fd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* create_text_file                                                     */
/* ------------------------------------------------------------------ */

int
tool_create_text_file(const struct policy *p,
                      const char *path, const char *content,
                      char *resp_buf, int resp_bufsz)
{
    char canonical[MCPSERVER_PATH_MAX];
    char epath[MCPSERVER_PATH_MAX * 2];

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_write_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"path\":\"%s\","
                        "\"created\":false,\"error\":null}", epath);
    }

    mcp_realpath(path, canonical);

    if (write_file(canonical, content, O_CREAT | O_EXCL) != 0) {
        if (errno == EEXIST) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,\"path\":\"%s\","
                            "\"created\":false,"
                            "\"error\":\"file already exists\"}", epath);
        }
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"created\":false,\"error\":\"write failed\"}", epath);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"path\":\"%s\","
                    "\"created\":true,\"error\":null}", epath);
}

/* ------------------------------------------------------------------ */
/* replace_text_file                                                    */
/* ------------------------------------------------------------------ */

int
tool_replace_text_file(const struct policy *p,
                       const char *path, const char *content,
                       char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    struct stat st;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_write_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"path\":\"%s\","
                        "\"replaced\":false,\"error\":null}", epath);
    }

    mcp_realpath(path, canonical);

    if (stat(canonical, &st) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"replaced\":false,"
                        "\"error\":\"file does not exist\"}", epath);
    }

    if (!S_ISREG(st.st_mode)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"replaced\":false,"
                        "\"error\":\"not a regular file\"}", epath);
    }

    if (write_file(canonical, content, O_TRUNC) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"replaced\":false,"
                        "\"error\":\"write failed\"}", epath);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"path\":\"%s\","
                    "\"replaced\":true,\"error\":null}", epath);
}

/* ------------------------------------------------------------------ */
/* make_directory                                                       */
/* ------------------------------------------------------------------ */

int
tool_make_directory(const struct policy *p, const char *path,
                    char *resp_buf, int resp_bufsz)
{
    char canonical[MCPSERVER_PATH_MAX];
    char epath[MCPSERVER_PATH_MAX * 2];

    json_escape(path, epath, sizeof(epath));

    /* directories have no extension — use the rw_root + deny check only */
    if (!policy_is_path_in_rw_root(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"path\":\"%s\","
                        "\"created\":false,\"error\":null}", epath);
    }

    mcp_realpath(path, canonical);

    if (mkdir(canonical, 0755) != 0) {
        if (errno == EEXIST) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,\"path\":\"%s\","
                            "\"created\":false,"
                            "\"error\":\"already exists\"}", epath);
        }
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"created\":false,"
                        "\"error\":\"mkdir failed\"}", epath);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"path\":\"%s\","
                    "\"created\":true,\"error\":null}", epath);
}

/* ------------------------------------------------------------------ */
/* delete_text_file                                                     */
/* ------------------------------------------------------------------ */

int
tool_delete_text_file(const struct policy *p, const char *path,
                      char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    struct stat st;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_write_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"path\":\"%s\","
                        "\"deleted\":false,\"error\":null}", epath);
    }

    mcp_realpath(path, canonical);

    if (stat(canonical, &st) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"deleted\":false,"
                        "\"error\":\"file does not exist\"}", epath);
    }
    if (!S_ISREG(st.st_mode)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"deleted\":false,"
                        "\"error\":\"not a regular file\"}", epath);
    }

    if (unlink(canonical) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"path\":\"%s\","
                        "\"deleted\":false,"
                        "\"error\":\"unlink failed\"}", epath);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"path\":\"%s\","
                    "\"deleted\":true,\"error\":null}", epath);
}

/* ------------------------------------------------------------------ */
/* rename_path                                                          */
/* ------------------------------------------------------------------ */

int
tool_rename_path(const struct policy *p,
                 const char *source, const char *dest,
                 char *resp_buf, int resp_bufsz)
{
    char  csrc[MCPSERVER_PATH_MAX];
    char  cdst[MCPSERVER_PATH_MAX];
    char  esrc[MCPSERVER_PATH_MAX * 2];
    char  edst[MCPSERVER_PATH_MAX * 2];
    int   src_ok, dst_ok;

    json_escape(source, esrc, sizeof(esrc));
    json_escape(dest,   edst, sizeof(edst));

    /*
     * Both source and dest must be writable (or at minimum in rw_root).
     * Source: policy_is_path_in_rw_root (existing file may lose its ext)
     * Dest:   policy_is_write_allowed if it's a file rename; use
     *         policy_is_path_in_rw_root to cover directory renames too.
     */
    src_ok = policy_is_path_in_rw_root(p, source);
    dst_ok = policy_is_path_in_rw_root(p, dest);

    if (!src_ok || !dst_ok) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,"
                        "\"source\":\"%s\",\"dest\":\"%s\","
                        "\"renamed\":false,\"error\":null}",
                        esrc, edst);
    }

    mcp_realpath(source, csrc);
    /*
     * dest may not exist yet; mcp_realpath will fail on a non-existent path.
     * Canonicalize only the parent directory, then append the filename.
     */
    {
        char  dparent[MCPSERVER_PATH_MAX];
        char  cparent[MCPSERVER_PATH_MAX];
        char *slash;
        const char *dname;

        strncpy(dparent, dest, sizeof(dparent) - 1);
        dparent[sizeof(dparent) - 1] = '\0';
        slash = strrchr(dparent, '/');
        if (slash) {
            *slash = '\0';
            dname  = dest + (slash - dparent) + 1;
        } else {
            dparent[0] = '.';
            dparent[1] = '\0';
            dname = dest;
        }
        if (!mcp_realpath(dparent, cparent)) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,"
                            "\"source\":\"%s\",\"dest\":\"%s\","
                            "\"renamed\":false,"
                            "\"error\":\"dest parent does not exist\"}",
                            esrc, edst);
        }
        if ((int)(strlen(cparent) + 1 + strlen(dname)) >= MCPSERVER_PATH_MAX) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,"
                            "\"source\":\"%s\",\"dest\":\"%s\","
                            "\"renamed\":false,"
                            "\"error\":\"dest path too long\"}",
                            esrc, edst);
        }
        strcpy(cdst, cparent);
        strcat(cdst, "/");
        strcat(cdst, dname);
    }

    if (rename(csrc, cdst) != 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,"
                        "\"source\":\"%s\",\"dest\":\"%s\","
                        "\"renamed\":false,\"error\":\"rename failed\"}",
                        esrc, edst);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,"
                    "\"source\":\"%s\",\"dest\":\"%s\","
                    "\"renamed\":true,\"error\":null}",
                    esrc, edst);
}
