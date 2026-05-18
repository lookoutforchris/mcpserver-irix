/*
 * tools_fs.c - filesystem read and inspect tools
 *
 * Implements: ping, path_exists, stat_path, list_directory,
 *             read_text_file, tail_text_file
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

/* ------------------------------------------------------------------ */
/* ping                                                                 */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* path_exists                                                          */
/* ------------------------------------------------------------------ */

int
tool_path_exists(const struct policy *p, const char *path,
                 char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    struct stat st;
    int         exists;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
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

/* ------------------------------------------------------------------ */
/* stat_path                                                            */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* list_directory                                                       */
/* ------------------------------------------------------------------ */

int
tool_list_directory(const struct policy *p, const char *path,
                    char *resp_buf, int resp_bufsz)
{
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    char        entry_path[MCPSERVER_PATH_MAX];
    char        ename[MCPSERVER_PATH_MAX * 2];
    struct stat st;
    DIR        *d;
    struct dirent *de;
    int         count = 0;
    int         truncated = 0;
    int         pos;          /* write position into resp_buf */
    const char *kind;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"entries\":[]}", epath);
    }

    mcp_realpath(path, canonical);
    d = opendir(canonical);
    if (!d) {
        if (errno == ENOTDIR) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,\"exists\":true,"
                            "\"path\":\"%s\",\"entries\":[],"
                            "\"error\":\"not a directory\"}", epath);
        }
        if (errno == ENOENT) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,\"exists\":false,"
                            "\"path\":\"%s\",\"entries\":[]}", epath);
        }
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":true,"
                        "\"path\":\"%s\",\"entries\":[],"
                        "\"error\":\"opendir failed\"}", epath);
    }

    /* start building response */
    pos = snprintf(resp_buf, (size_t)resp_bufsz,
                   "{\"allowed\":true,\"exists\":true,"
                   "\"path\":\"%s\",\"entries\":[", epath);

    while ((de = readdir(d)) != NULL) {
        /* skip . and .. */
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        /* build full path for deny check */
        if ((int)(strlen(canonical) + 1 + strlen(de->d_name)) >=
                MCPSERVER_PATH_MAX)
            continue;
        strcpy(entry_path, canonical);
        strcat(entry_path, "/");
        strcat(entry_path, de->d_name);

        /* apply deny policy to each entry */
        if (!policy_is_read_allowed(p, entry_path))
            continue;

        if (count >= MCP_LIST_MAX) {
            truncated = 1;
            break;
        }

        /* get kind via stat */
        if (stat(entry_path, &st) == 0) {
            if (S_ISREG(st.st_mode))      kind = "file";
            else if (S_ISDIR(st.st_mode)) kind = "directory";
            else                           kind = "other";
        } else {
            kind = "other";
        }

        json_escape(de->d_name, ename, sizeof(ename));

        /* append entry JSON, checking we don't overflow */
        if (pos + (int)(strlen(ename) + 32) < resp_bufsz) {
            pos += snprintf(resp_buf + pos, (size_t)(resp_bufsz - pos),
                            "%s{\"name\":\"%s\",\"kind\":\"%s\"}",
                            count > 0 ? "," : "", ename, kind);
        }
        count++;
    }
    closedir(d);

    snprintf(resp_buf + pos, (size_t)(resp_bufsz - pos),
             "],\"truncated\":%s}", truncated ? "true" : "false");

    return (int)strlen(resp_buf);
}

/* ------------------------------------------------------------------ */
/* read_text_file                                                       */
/* ------------------------------------------------------------------ */

/*
 * read_text_file_impl - shared by read_text_file and tail_text_file.
 * Reads lines [first_line, first_line+n_lines) from f into content_buf
 * (up to content_bufsz-1 bytes). Returns lines actually read.
 * Sets *truncated if content was clipped.
 */
static int
read_lines(FILE *f, int first_line, int max_lines,
           char *content_buf, int content_bufsz, int *truncated)
{
    char line[MCP_LINE_MAX];
    int  cur = 1;
    int  lines_read = 0;
    int  pos = 0;
    int  n;

    *truncated = 0;

    /* skip to first_line */
    while (cur < first_line) {
        if (!fgets(line, sizeof(line), f)) return 0;
        /* advance cur only on actual line ends to handle long lines */
        if (strchr(line, '\n')) cur++;
    }

    /* read up to max_lines lines */
    while (lines_read < max_lines) {
        if (!fgets(line, sizeof(line), f)) break;
        n = (int)strlen(line);

        if (pos + n >= content_bufsz - 1) {
            /* clip */
            int avail = content_bufsz - 1 - pos;
            if (avail > 0) {
                memcpy(content_buf + pos, line, (size_t)avail);
                pos += avail;
            }
            *truncated = 1;
            /* drain the rest of this logical line if it had no newline */
            break;
        }

        memcpy(content_buf + pos, line, (size_t)n);
        pos += n;

        if (strchr(line, '\n')) lines_read++;
    }

    content_buf[pos] = '\0';

    /* also truncate if raw content exceeds MCP_CONTENT_MAX */
    if (pos >= MCP_CONTENT_MAX) {
        content_buf[MCP_CONTENT_MAX] = '\0';
        *truncated = 1;
    }

    return lines_read;
}

int
tool_read_text_file(const struct policy *p, const char *path,
                    int start_line, int max_lines,
                    char *resp_buf, int resp_bufsz)
{
    static char content[MCP_CONTENT_MAX + 1];
    static char escaped[MCP_CONTENT_MAX * 2 + 1];
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    FILE       *f;
    int         lines_read, truncated;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"start_line\":1,\"lines_returned\":0,"
                        "\"truncated\":false,\"error\":null}", epath);
    }

    if (start_line < 1)  start_line = 1;
    if (max_lines  < 1)  max_lines  = MCP_DEFAULT_READ_LINES;
    if (max_lines  > 500) max_lines = 500;

    mcp_realpath(path, canonical);
    f = fopen(canonical, "r");
    if (!f) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":%s,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"start_line\":%d,\"lines_returned\":0,"
                        "\"truncated\":false,\"error\":\"cannot open file\"}",
                        errno == ENOENT ? "false" : "true",
                        epath, start_line);
    }

    lines_read = read_lines(f, start_line, max_lines,
                            content, sizeof(content), &truncated);
    fclose(f);

    json_escape(content, escaped, sizeof(escaped));

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":true,"
                    "\"path\":\"%s\",\"content\":\"%s\","
                    "\"start_line\":%d,\"lines_returned\":%d,"
                    "\"truncated\":%s,\"error\":null}",
                    epath, escaped, start_line, lines_read,
                    truncated ? "true" : "false");
}

/* ------------------------------------------------------------------ */
/* tail_text_file                                                       */
/* ------------------------------------------------------------------ */

int
tool_tail_text_file(const struct policy *p, const char *path,
                    int n_lines,
                    char *resp_buf, int resp_bufsz)
{
    /*
     * Strategy: read entire file (up to a ceiling) into a buffer,
     * then walk backwards from the end to find the last n_lines newlines,
     * and return from that offset forward.
     *
     * File read ceiling: MCP_CONTENT_MAX * 4 (80KB). Files larger than
     * this are tailed from within the last 80KB only.
     */
    static char  filebuf[MCP_CONTENT_MAX * 4 + 1];
    static char  escaped[MCP_CONTENT_MAX * 2 + 1];
    char         canonical[MCPSERVER_PATH_MAX];
    char         epath[MCPSERVER_PATH_MAX * 2];
    FILE        *f;
    long         fsize;
    long         readfrom;
    int          nbytes;
    int          newlines;
    int          i;
    const char  *start;
    int          truncated = 0;
    int          start_line = 1;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"start_line\":1,\"lines_returned\":0,"
                        "\"truncated\":false,\"error\":null}", epath);
    }

    if (n_lines < 1)  n_lines = MCP_DEFAULT_TAIL_LINES;
    if (n_lines > 500) n_lines = 500;

    mcp_realpath(path, canonical);
    f = fopen(canonical, "r");
    if (!f) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":%s,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"start_line\":1,\"lines_returned\":0,"
                        "\"truncated\":false,\"error\":\"cannot open file\"}",
                        errno == ENOENT ? "false" : "true", epath);
    }

    fseek(f, 0L, SEEK_END);
    fsize = ftell(f);

    readfrom = fsize - (long)sizeof(filebuf) + 1;
    if (readfrom < 0) readfrom = 0;

    fseek(f, readfrom, SEEK_SET);
    nbytes = (int)fread(filebuf, 1, sizeof(filebuf) - 1, f);
    fclose(f);
    filebuf[nbytes] = '\0';

    /* find start of last n_lines by counting newlines from end */
    newlines = 0;
    start = filebuf + nbytes;

    for (i = nbytes - 1; i >= 0; i--) {
        if (filebuf[i] == '\n') {
            newlines++;
            if (newlines == n_lines) {
                start = filebuf + i + 1;
                break;
            }
        }
    }

    if (readfrom > 0) truncated = 1;

    /* clip to MCP_CONTENT_MAX */
    if ((int)strlen(start) > MCP_CONTENT_MAX) {
        filebuf[start - filebuf + MCP_CONTENT_MAX] = '\0';
        truncated = 1;
    }

    json_escape(start, escaped, sizeof(escaped));

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":true,"
                    "\"path\":\"%s\",\"content\":\"%s\","
                    "\"start_line\":%d,\"lines_returned\":%d,"
                    "\"truncated\":%s,\"error\":null}",
                    epath, escaped, start_line, newlines,
                    truncated ? "true" : "false");
}
