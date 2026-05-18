/*
 * policy.c - boundary policy loader and enforcement
 *
 * Parses /etc/mcpserver/boundaries.json and answers authorization
 * questions for the tool dispatch layer.
 *
 * STUB: JSON parsing for each field is not yet implemented.
 * Structure, function signatures, and authorization logic are final.
 */

#include "policy.h"
#include "result.h"
#include "../compat/compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>

/* Hardcoded global deny globs applied to every read and write. */
static const char *GLOBAL_DENY[] = {
    "**/.env",
    "**/.env.*",
    "**/*.secret",
    "**/*.key",
    "**/*.pem",
    "**/*.crt",
    "**/*_rsa",
    "**/*_dsa",
    NULL
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * path_matches_deny - check if canonical path matches any deny pattern.
 * Patterns may be absolute paths, path prefixes, or mcp_fnmatch globs.
 * Returns 1 if matched (denied), 0 if not.
 */
static int
path_matches_deny(const char *path, const char patterns[][POLICY_PATTERN_MAX],
                  int count)
{
    int i;
    for (i = 0; i < count; i++) {
        const char *pat = patterns[i];
        if (!pat || !pat[0])
            continue;
        /* absolute prefix match */
        if (pat[0] == '/' && strncmp(path, pat, strlen(pat)) == 0)
            return 1;
        /* glob match against full path */
        if (mcp_fnmatch(pat, path, 0) == 0)
            return 1;
    }
    return 0;
}

static int
path_matches_global_deny(const char *path)
{
    int i;
    for (i = 0; GLOBAL_DENY[i]; i++) {
        if (mcp_fnmatch(GLOBAL_DENY[i], path, 0) == 0)
            return 1;
    }
    return 0;
}

/*
 * is_under_roots - check if path is under any root in the list.
 * Returns 1 if found, 0 otherwise.
 */
static int
is_under_roots(const char path[][MCPSERVER_PATH_MAX], int count,
               const char *target)
{
    int    i;
    size_t rlen;

    for (i = 0; i < count; i++) {
        rlen = strlen(path[i]);
        if (rlen == 0) continue;
        if (strncmp(target, path[i], rlen) == 0) {
            if (target[rlen] == '/' || target[rlen] == '\0')
                return 1;
        }
    }
    return 0;
}

/*
 * write_ext_allowed - check file extension/name against write allowlist.
 */
static int
write_ext_allowed(const struct policy *p, const char *path)
{
    const char *base;
    const char *ext;
    int i;

    base = strrchr(path, '/');
    base = base ? base + 1 : path;

    /* exact name match */
    for (i = 0; i < p->allow_name_count; i++) {
        if (strcmp(base, p->allow_name[i]) == 0)
            return 1;
    }

    /* extension match */
    ext = strrchr(base, '.');
    if (!ext)
        return 0;
    for (i = 0; i < p->allow_ext_count; i++) {
        if (strcmp(ext, p->allow_ext[i]) == 0)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int
policy_load(const char *path, struct policy *p)
{
    FILE *f;
    char  buf[65536];
    int   n;

    if (!path || !p) {
        syslog(LOG_ERR, "policy_load: NULL argument");
        return -1;
    }

    memset(p, 0, sizeof(*p));
    strcpy(p->profile, "full"); /* default */

    f = fopen(path, "r");
    if (!f) {
        syslog(LOG_ERR, "policy_load: cannot open %s", path);
        return -1;
    }

    n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    if (n <= 0) {
        syslog(LOG_ERR, "policy_load: empty or unreadable: %s", path);
        return -1;
    }
    buf[n] = '\0';

    /*
     * TODO: parse boundaries.json fields:
     *   read_write_roots  -> p->rw_roots[]
     *   read_only_roots   -> p->ro_roots[]
     *   deny_overrides    -> p->deny[]
     *   write_rules.deny_write_globs -> p->write_deny[]
     *   read_rules.deny_read_globs   -> p->read_deny[]
     *   write_rules.allow_create_extensions -> p->allow_ext[]
     *   write_rules.allow_create_names      -> p->allow_name[]
     *   shell_rules.allowed_commands        -> p->allowed_cmds[]
     *   profile (env var override)          -> p->profile
     *
     * Using json_get_string / json arrays once the JSON parser supports
     * array extraction.
     */

    syslog(LOG_INFO, "policy_load: loaded %s", path);
    return 0;
}

int
policy_is_read_allowed(const struct policy *p, const char *path)
{
    char canonical[MCPSERVER_PATH_MAX];

    if (!p || !path) return 0;

    if (!mcp_realpath(path, canonical)) return 0;

    /* 1. global deny */
    if (path_matches_global_deny(canonical)) return 0;

    /* 2. per-policy deny overrides */
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->deny,
            p->deny_count))
        return 0;

    /* 3. read deny globs */
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->read_deny,
            p->read_deny_count))
        return 0;

    /* 4. must be under an allowed root */
    if (is_under_roots((const char (*)[MCPSERVER_PATH_MAX])p->rw_roots,
                       p->rw_count, canonical))
        return 1;
    if (is_under_roots((const char (*)[MCPSERVER_PATH_MAX])p->ro_roots,
                       p->ro_count, canonical))
        return 1;

    return 0;
}

int
policy_is_write_allowed(const struct policy *p, const char *path)
{
    char canonical[MCPSERVER_PATH_MAX];

    if (!p || !path) return 0;

    if (!mcp_realpath(path, canonical)) return 0;

    /* 1. global deny */
    if (path_matches_global_deny(canonical)) return 0;

    /* 2. per-policy deny overrides */
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->deny,
            p->deny_count))
        return 0;

    /* 3. write deny globs */
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->write_deny,
            p->write_deny_count))
        return 0;

    /* 4. must be under a read-write root (not read-only) */
    if (!is_under_roots((const char (*)[MCPSERVER_PATH_MAX])p->rw_roots,
                        p->rw_count, canonical))
        return 0;

    /* 5. extension or name allowlist */
    if (!write_ext_allowed(p, canonical)) return 0;

    return 1;
}

int
policy_is_cmd_allowed(const struct policy *p, const char *command)
{
    int i;
    if (!p || !command) return 0;
    for (i = 0; i < p->cmd_count; i++) {
        if (strcmp(p->allowed_cmds[i], command) == 0)
            return 1;
    }
    return 0;
}

int
policy_is_full_profile(const struct policy *p)
{
    if (!p) return 0;
    return strcmp(p->profile, "full") == 0;
}
