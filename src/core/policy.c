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
#include "json.h"
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
    char  sub[32768]; /* sub-object buffer for write_rules etc. */
    int   n;
    char *env_profile;

    if (!path || !p) {
        syslog(LOG_ERR, "policy_load: NULL argument");
        return -1;
    }

    memset(p, 0, sizeof(*p));
    strcpy(p->profile, "full"); /* default */

    /* allow env var override for profile (useful for read-only SSH sessions) */
    env_profile = getenv("MCPSERVER_PROFILE");
    if (env_profile &&
        (strcmp(env_profile, "full") == 0 ||
         strcmp(env_profile, "readonly") == 0))
        strncpy(p->profile, env_profile, sizeof(p->profile) - 1);

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

    /* top-level string arrays */
    p->rw_count = json_get_string_array(buf, "read_write_roots",
                      (char *)p->rw_roots, MCPSERVER_PATH_MAX, POLICY_ROOTS_MAX);
    if (p->rw_count < 0) p->rw_count = 0;

    p->ro_count = json_get_string_array(buf, "read_only_roots",
                      (char *)p->ro_roots, MCPSERVER_PATH_MAX, POLICY_ROOTS_MAX);
    if (p->ro_count < 0) p->ro_count = 0;

    p->deny_count = json_get_string_array(buf, "deny_overrides",
                        (char *)p->deny, POLICY_PATTERN_MAX, POLICY_DENY_MAX);
    if (p->deny_count < 0) p->deny_count = 0;

    /* write_rules sub-object */
    if (json_get_object(buf, "write_rules", sub, sizeof(sub)) == 0) {
        p->allow_ext_count = json_get_string_array(sub, "allow_create_extensions",
                                 (char *)p->allow_ext, 32, POLICY_EXT_MAX);
        if (p->allow_ext_count < 0) p->allow_ext_count = 0;

        p->allow_name_count = json_get_string_array(sub, "allow_create_names",
                                  (char *)p->allow_name, 64, POLICY_NAME_MAX);
        if (p->allow_name_count < 0) p->allow_name_count = 0;

        p->write_deny_count = json_get_string_array(sub, "deny_write_globs",
                                  (char *)p->write_deny, POLICY_PATTERN_MAX,
                                  POLICY_DENY_MAX);
        if (p->write_deny_count < 0) p->write_deny_count = 0;
    }

    /* read_rules sub-object */
    if (json_get_object(buf, "read_rules", sub, sizeof(sub)) == 0) {
        p->read_deny_count = json_get_string_array(sub, "deny_read_globs",
                                 (char *)p->read_deny, POLICY_PATTERN_MAX,
                                 POLICY_DENY_MAX);
        if (p->read_deny_count < 0) p->read_deny_count = 0;
    }

    /* shell_rules sub-object */
    if (json_get_object(buf, "shell_rules", sub, sizeof(sub)) == 0) {
        p->cmd_count = json_get_string_array(sub, "allowed_commands",
                           (char *)p->allowed_cmds, 32, POLICY_CMDS_MAX);
        if (p->cmd_count < 0) p->cmd_count = 0;
    }

    syslog(LOG_INFO,
           "policy_load: loaded %s (rw=%d ro=%d deny=%d cmds=%d profile=%s)",
           path, p->rw_count, p->ro_count, p->deny_count,
           p->cmd_count, p->profile);
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
policy_is_path_in_rw_root(const struct policy *p, const char *path)
{
    char canonical[MCPSERVER_PATH_MAX];

    if (!p || !path) return 0;

    if (!mcp_realpath(path, canonical)) return 0;

    if (path_matches_global_deny(canonical)) return 0;
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->deny,
            p->deny_count))
        return 0;
    if (path_matches_deny(canonical,
            (const char (*)[POLICY_PATTERN_MAX])p->write_deny,
            p->write_deny_count))
        return 0;

    return is_under_roots((const char (*)[MCPSERVER_PATH_MAX])p->rw_roots,
                          p->rw_count, canonical);
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
