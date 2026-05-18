/*
 * tools_text.c - text search and pattern context tools
 *
 * Implements: search_text, read_text_around_pattern, safe_json_preview
 */

#include "tools_text.h"
#include "result.h"
#include "json.h"
#include "protocol.h"    /* PROTO_RESP_MAX */
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Portable case-insensitive substring search                           */
/* ------------------------------------------------------------------ */

/*
 * str_icontains - case-insensitive substring search.
 * Returns pointer to first occurrence of needle in haystack, or NULL.
 */
static const char *
str_icontains(const char *haystack, const char *needle)
{
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    size_t i;
    size_t j;

    if (nlen == 0) return haystack;
    if (nlen > hlen) return NULL;

    for (i = 0; i <= hlen - nlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen) return haystack + i;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* search_text                                                          */
/* ------------------------------------------------------------------ */

/*
 * file_matches_globs - check if filename matches any glob in include_globs.
 * If include_globs is NULL or empty, all files match.
 */
static int
file_matches_globs(const char *name, const char **include_globs)
{
    int i;
    if (!include_globs || !include_globs[0]) return 1;
    for (i = 0; include_globs[i]; i++) {
        if (mcp_fnmatch(include_globs[i], name, 0) == 0) return 1;
    }
    return 0;
}

/*
 * search_file - search one file for pattern, appending matches to out_buf.
 * Returns number of new matches found.
 */
static int
search_file(const struct policy *p,
            const char *fpath,
            const char *pattern, int case_sensitive,
            char *out_buf, int out_bufsz, int *out_pos,
            int match_count, int max_results)
{
    char        line[MCP_LINE_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    char        eline[MCP_LINE_MAX * 2];
    FILE       *f;
    int         lineno = 0;
    int         found = 0;
    const char *hit;

    if (match_count >= max_results) return 0;

    f = fopen(fpath, "r");
    if (!f) return 0;

    json_escape(fpath, epath, sizeof(epath));

    while (fgets(line, sizeof(line), f) && match_count + found < max_results) {
        size_t llen;
        lineno++;

        /* strip trailing newline for the match line display */
        llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\n') line[--llen] = '\0';
        if (llen > 0 && line[llen - 1] == '\r') line[--llen] = '\0';

        if (case_sensitive)
            hit = strstr(line, pattern);
        else
            hit = str_icontains(line, pattern);

        if (!hit) continue;

        json_escape(line, eline, sizeof(eline));

        /* append match entry if space remains */
        if (*out_pos + (int)(strlen(epath) + strlen(eline) + 64) < out_bufsz) {
            *out_pos += snprintf(out_buf + *out_pos,
                                 (size_t)(out_bufsz - *out_pos),
                                 "%s{\"path\":\"%s\","
                                  "\"line_number\":%d,"
                                  "\"line\":\"%s\"}",
                                 (match_count + found > 0) ? "," : "",
                                 epath, lineno, eline);
        }
        found++;
    }
    fclose(f);

    /* suppress unused warning */
    (void)p;
    return found;
}

/*
 * search_dir_recursive - walk a directory tree searching for pattern.
 */
static void
search_dir_recursive(const struct policy *p,
                     const char *dirpath,
                     const char *pattern,
                     const char **include_globs,
                     int case_sensitive,
                     char *out_buf, int out_bufsz, int *out_pos,
                     int *match_count, int max_results)
{
    char        entry_path[MCPSERVER_PATH_MAX];
    DIR        *d;
    struct dirent *de;
    struct stat st;

    if (*match_count >= max_results) return;

    d = opendir(dirpath);
    if (!d) return;

    while ((de = readdir(d)) != NULL && *match_count < max_results) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        if ((int)(strlen(dirpath) + 1 + strlen(de->d_name)) >= MCPSERVER_PATH_MAX)
            continue;

        strcpy(entry_path, dirpath);
        strcat(entry_path, "/");
        strcat(entry_path, de->d_name);

        /* apply read policy */
        if (!policy_is_read_allowed(p, entry_path)) continue;

        if (stat(entry_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            search_dir_recursive(p, entry_path, pattern, include_globs,
                                 case_sensitive, out_buf, out_bufsz, out_pos,
                                 match_count, max_results);
        } else if (S_ISREG(st.st_mode)) {
            if (!file_matches_globs(de->d_name, include_globs)) continue;
            *match_count += search_file(p, entry_path, pattern, case_sensitive,
                                        out_buf, out_bufsz, out_pos,
                                        *match_count, max_results);
        }
    }
    closedir(d);
}

int
tool_search_text(const struct policy *p,
                 const char *root_path,
                 const char *pattern,
                 const char **include_globs,
                 int max_results,
                 int case_sensitive,
                 char *resp_buf, int resp_bufsz)
{
    static char matches_buf[PROTO_RESP_MAX / 2];
    char        canonical[MCPSERVER_PATH_MAX];
    char        eroot[MCPSERVER_PATH_MAX * 2];
    char        epat[512];
    int         match_count = 0;
    int         matches_pos = 0;
    int         truncated = 0;

    json_escape(root_path, eroot, sizeof(eroot));
    json_escape(pattern,   epat,  sizeof(epat));

    if (!policy_is_read_allowed(p, root_path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"root_path\":\"%s\","
                        "\"pattern\":\"%s\",\"matches\":[],"
                        "\"truncated\":false}", eroot, epat);
    }

    if (max_results < 1)   max_results = MCP_DEFAULT_SEARCH_MAX;
    if (max_results > MCP_SEARCH_MAX) max_results = MCP_SEARCH_MAX;

    mcp_realpath(root_path, canonical);
    matches_buf[0] = '\0';

    search_dir_recursive(p, canonical, pattern, include_globs, case_sensitive,
                         matches_buf, (int)sizeof(matches_buf), &matches_pos,
                         &match_count, max_results);

    truncated = (match_count >= max_results);

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"root_path\":\"%s\","
                    "\"pattern\":\"%s\",\"matches\":[%s],"
                    "\"truncated\":%s}",
                    eroot, epat, matches_buf,
                    truncated ? "true" : "false");
}

/* ------------------------------------------------------------------ */
/* read_text_around_pattern                                             */
/* ------------------------------------------------------------------ */

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
    /*
     * Strategy: read file into a ring buffer of line pointers, find the
     * Nth match, then emit context_before..match..context_after.
     * File capped at 256KB to bound memory use.
     */
#define CTX_MAX_LINES  1024
#define CTX_FILE_MAX   (256 * 1024)

    static char  filebuf[CTX_FILE_MAX + 1];
    static char  content[MCP_CONTENT_MAX + 1];
    static char  escaped[MCP_CONTENT_MAX * 2 + 1];
    static char *line_starts[CTX_MAX_LINES];
    char         canonical[MCPSERVER_PATH_MAX];
    char         epath[MCPSERVER_PATH_MAX * 2];

    FILE       *f;
    int         nbytes;
    int         nlines = 0;
    int         occurrence = 0;
    int         match_line = -1;
    int         i;
    int         from_line, to_line;
    int         pos;
    const char *hit;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"found\":false,"
                        "\"match_line\":0,\"content\":\"\","
                        "\"error\":null}", epath);
    }

    if (match_index < 1) match_index = 1;

    mcp_realpath(path, canonical);
    f = fopen(canonical, "r");
    if (!f) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":%s,"
                        "\"path\":\"%s\",\"found\":false,"
                        "\"match_line\":0,\"content\":\"\","
                        "\"error\":\"cannot open file\"}",
                        errno == ENOENT ? "false" : "true", epath);
    }

    nbytes = (int)fread(filebuf, 1, CTX_FILE_MAX, f);
    fclose(f);
    filebuf[nbytes] = '\0';

    /* index line starts */
    if (nbytes > 0) {
        line_starts[nlines++] = filebuf;
        for (i = 0; i < nbytes && nlines < CTX_MAX_LINES - 1; i++) {
            if (filebuf[i] == '\n' && i + 1 < nbytes)
                line_starts[nlines++] = filebuf + i + 1;
        }
    }

    /* find Nth occurrence */
    for (i = 0; i < nlines && match_line < 0; i++) {
        char *end;
        char  saved;
        /* null-terminate this line temporarily */
        end = (i + 1 < nlines) ? line_starts[i + 1] - 1 : filebuf + nbytes;
        saved = *end;
        *end = '\0';

        if (case_sensitive)
            hit = strstr(line_starts[i], pattern);
        else
            hit = str_icontains(line_starts[i], pattern);

        *end = saved;

        if (hit) {
            occurrence++;
            if (occurrence == match_index)
                match_line = i; /* 0-based index */
        }
    }

    if (match_line < 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":true,"
                        "\"path\":\"%s\",\"found\":false,"
                        "\"match_line\":0,\"content\":\"\","
                        "\"error\":null}", epath);
    }

    /* compute window */
    from_line = match_line - context_before;
    if (from_line < 0) from_line = 0;
    to_line = match_line + context_after;
    if (to_line >= nlines) to_line = nlines - 1;

    /* build content string */
    pos = 0;
    for (i = from_line; i <= to_line && pos < MCP_CONTENT_MAX; i++) {
        char *end;
        char  saved;
        int   llen;

        end = (i + 1 < nlines) ? line_starts[i + 1] - 1 : filebuf + nbytes;
        saved = *end;
        *end = '\0';
        llen = (int)strlen(line_starts[i]);
        if (pos + llen >= MCP_CONTENT_MAX) llen = MCP_CONTENT_MAX - pos;
        memcpy(content + pos, line_starts[i], (size_t)llen);
        pos += llen;
        *end = saved;

        if (pos < MCP_CONTENT_MAX) content[pos++] = '\n';
    }
    content[pos] = '\0';

    json_escape(content, escaped, sizeof(escaped));

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":true,"
                    "\"path\":\"%s\",\"found\":true,"
                    "\"match_line\":%d,\"content\":\"%s\","
                    "\"error\":null}",
                    epath, match_line + 1, /* 1-based for output */ escaped);
}

/* ------------------------------------------------------------------ */
/* safe_json_preview                                                    */
/* ------------------------------------------------------------------ */

/*
 * summarise_value - produce a short type summary for a JSON value.
 * val points at the start of a JSON value. Writes to out[outsz].
 * Returns bytes written.
 */
static int
summarise_value(const char *val, char *out, int outsz)
{
    int depth, n;

    val = val; /* already at start */

    if (!val || *val == '\0')
        return snprintf(out, (size_t)outsz, "null");

    if (*val == '"')
        return snprintf(out, (size_t)outsz, "\"...\"");

    if (*val == '[') {
        /* count items */
        int items = 0;
        const char *p = val + 1;
        depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
            } else if (*p == '[' || *p == '{') depth++;
            else if (*p == ']' || *p == '}') { depth--; if (depth == 0) break; }
            else if (*p == ',' && depth == 1) items++;
            if (*p) p++;
        }
        if (items > 0 || (p > val + 1 && *p == ']')) items++;
        return snprintf(out, (size_t)outsz, "[<%d item%s>]",
                        items, items == 1 ? "" : "s");
    }

    if (*val == '{') {
        /* count keys */
        int keys = 0;
        const char *p = val + 1;
        depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') {
                if (depth == 1) keys++;
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p) p++; /* closing " */
                /* skip : value */
                if (depth == 1) {
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    if (*p == ':') p++;
                }
                continue;
            } else if (*p == '[' || *p == '{') depth++;
            else if (*p == ']' || *p == '}') { depth--; if (depth == 0) break; }
            if (*p) p++;
        }
        return snprintf(out, (size_t)outsz, "{<%d key%s>}",
                        keys, keys == 1 ? "" : "s");
    }

    /* number, boolean, null: copy as-is up to delimiter */
    n = 0;
    while (*val && *val != ',' && *val != '}' && *val != ']' &&
           *val != ' ' && *val != '\n' && *val != '\t' && n < outsz - 1)
        out[n++] = *val++;
    out[n] = '\0';
    return n;
}

int
tool_safe_json_preview(const struct policy *p,
                       const char *path,
                       int top_level_only,
                       int max_bytes,
                       char *resp_buf, int resp_bufsz)
{
    static char filebuf[50001];
    static char out_json[MCP_CONTENT_MAX + 1];
    static char escaped[MCP_CONTENT_MAX * 2 + 1];
    char        canonical[MCPSERVER_PATH_MAX];
    char        epath[MCPSERVER_PATH_MAX * 2];
    FILE       *f;
    int         nbytes;
    int         truncated = 0;

    json_escape(path, epath, sizeof(epath));

    if (!policy_is_read_allowed(p, path)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"exists\":false,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"truncated\":false,\"error\":null}", epath);
    }

    if (max_bytes < 1)     max_bytes = MCP_DEFAULT_JSON_BYTES;
    if (max_bytes > 50000) max_bytes = 50000;

    mcp_realpath(path, canonical);
    f = fopen(canonical, "r");
    if (!f) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":%s,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"truncated\":false,"
                        "\"error\":\"cannot open file\"}",
                        errno == ENOENT ? "false" : "true", epath);
    }

    nbytes = (int)fread(filebuf, 1, (size_t)max_bytes, f);
    if (nbytes == max_bytes) truncated = 1;
    fclose(f);
    if (nbytes <= 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"exists\":true,"
                        "\"path\":\"%s\",\"content\":\"\","
                        "\"truncated\":false,"
                        "\"error\":\"empty file\"}", epath);
    }
    filebuf[nbytes] = '\0';

    /* validate it starts like JSON */
    {
        int i = 0;
        while (i < nbytes && (filebuf[i] == ' ' || filebuf[i] == '\t' ||
               filebuf[i] == '\n' || filebuf[i] == '\r')) i++;
        if (i >= nbytes || (filebuf[i] != '{' && filebuf[i] != '[')) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                            "{\"allowed\":true,\"exists\":true,"
                            "\"path\":\"%s\",\"content\":\"\","
                            "\"truncated\":false,"
                            "\"error\":\"not valid JSON\"}", epath);
        }
    }

    if (top_level_only && filebuf[0] == '{') {
        /*
         * Build a summarised version of the top-level object:
         * show each key with a short value description.
         */
        int         pos = 0;
        const char *p2  = filebuf + 1;
        char        key[128];
        char        val_summary[64];
        int         first = 1;
        int         depth;

        out_json[pos++] = '{';

        while (*p2 && pos < MCP_CONTENT_MAX - 4) {
            /* skip whitespace and commas */
            while (*p2 == ' ' || *p2 == '\t' || *p2 == '\n' ||
                   *p2 == '\r' || *p2 == ',') p2++;
            if (*p2 == '}' || *p2 == '\0') break;
            if (*p2 != '"') break;

            /* read key */
            p2++;
            {
                int ki = 0;
                while (*p2 && *p2 != '"' && ki < (int)sizeof(key) - 1) {
                    if (*p2 == '\\') p2++;
                    if (*p2) key[ki++] = *p2++;
                }
                key[ki] = '\0';
                if (*p2 == '"') p2++;
            }

            /* skip colon */
            while (*p2 == ' ' || *p2 == '\t') p2++;
            if (*p2 == ':') p2++;
            while (*p2 == ' ' || *p2 == '\t') p2++;

            /* summarise value */
            summarise_value(p2, val_summary, (int)sizeof(val_summary));

            /* skip actual value */
            if (*p2 == '{' || *p2 == '[') {
                depth = 1;
                p2++;
                while (*p2 && depth > 0) {
                    if (*p2 == '"') {
                        p2++;
                        while (*p2 && *p2 != '"') {
                            if (*p2 == '\\') p2++;
                            if (*p2) p2++;
                        }
                        if (*p2) p2++;
                        continue;
                    }
                    if (*p2 == '{' || *p2 == '[') depth++;
                    else if (*p2 == '}' || *p2 == ']') depth--;
                    if (depth > 0 || *p2 == '}' || *p2 == ']') p2++;
                    else break;
                }
                if (*p2) p2++;
            } else if (*p2 == '"') {
                p2++;
                while (*p2 && *p2 != '"') {
                    if (*p2 == '\\') p2++;
                    if (*p2) p2++;
                }
                if (*p2) p2++;
            } else {
                while (*p2 && *p2 != ',' && *p2 != '}') p2++;
            }

            if (!first) out_json[pos++] = ',';
            first = 0;

            pos += snprintf(out_json + pos, (size_t)(MCP_CONTENT_MAX - pos),
                            "\"%s\": %s", key, val_summary);
        }
        out_json[pos++] = '}';
        out_json[pos]   = '\0';
    } else {
        /* return raw content (already capped at max_bytes) */
        strncpy(out_json, filebuf, MCP_CONTENT_MAX);
        out_json[MCP_CONTENT_MAX] = '\0';
    }

    json_escape(out_json, escaped, sizeof(escaped));

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"exists\":true,"
                    "\"path\":\"%s\",\"content\":\"%s\","
                    "\"truncated\":%s,\"error\":null}",
                    epath, escaped, truncated ? "true" : "false");
}
