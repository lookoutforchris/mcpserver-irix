/*
 * policy.h - boundary policy loader and enforcement
 *
 * Loads /etc/mcpserver/boundaries.json and answers path/write/command
 * authorization questions for every tool call. All tool code must go
 * through these functions before touching the filesystem or spawning
 * a subprocess.
 *
 * See docs/SECURITY_MODEL.md for the full authorization model.
 */

#ifndef MCPSERVER_POLICY_H
#define MCPSERVER_POLICY_H

#include "../compat/compat.h"

/* Maximum entries in each list */
#define POLICY_ROOTS_MAX      64
#define POLICY_DENY_MAX       256
#define POLICY_EXT_MAX        64
#define POLICY_NAME_MAX       32
#define POLICY_CMDS_MAX       32
#define POLICY_PATTERN_MAX    256

/*
 * Loaded boundary policy.
 * Populated by policy_load(); consulted by policy_is_* functions.
 */
struct policy {
    /* read-write and read-only root paths */
    char rw_roots[POLICY_ROOTS_MAX][MCPSERVER_PATH_MAX];
    int  rw_count;

    char ro_roots[POLICY_ROOTS_MAX][MCPSERVER_PATH_MAX];
    int  ro_count;

    /* deny override patterns (absolute paths or globs) */
    char deny[POLICY_DENY_MAX][POLICY_PATTERN_MAX];
    int  deny_count;

    /* write deny globs */
    char write_deny[POLICY_DENY_MAX][POLICY_PATTERN_MAX];
    int  write_deny_count;

    /* read deny globs */
    char read_deny[POLICY_DENY_MAX][POLICY_PATTERN_MAX];
    int  read_deny_count;

    /* write extension allowlist */
    char allow_ext[POLICY_EXT_MAX][32];
    int  allow_ext_count;

    /* write name allowlist (exact filenames, e.g. "Makefile") */
    char allow_name[POLICY_NAME_MAX][64];
    int  allow_name_count;

    /* allowed shell commands */
    char allowed_cmds[POLICY_CMDS_MAX][32];
    int  cmd_count;

    /* tool profile: "full" or "readonly" */
    char profile[16];
};

/*
 * policy_load - load and parse boundaries.json into p.
 * Returns 0 on success, -1 on error (logs reason via syslog).
 */
int policy_load(const char *path, struct policy *p);

/*
 * policy_is_read_allowed - check if path may be read.
 *
 * Canonicalizes path via mcp_realpath, then:
 *   1. Deny if matches any deny[] or read_deny[] pattern.
 *   2. Allow if under any rw_root or ro_root.
 *   3. Deny by default.
 *
 * Returns 1 if allowed, 0 if denied.
 */
int policy_is_read_allowed(const struct policy *p, const char *path);

/*
 * policy_is_write_allowed - check if path may be written.
 *
 * Same as read check, plus:
 *   - path must be under a rw_root (not ro_root).
 *   - file extension or basename must be in the allowlist.
 *   - path must not match write_deny[] patterns.
 *
 * Returns 1 if allowed, 0 if denied.
 */
int policy_is_write_allowed(const struct policy *p, const char *path);

/*
 * policy_is_cmd_allowed - check if command is in the allowed list.
 * Returns 1 if allowed, 0 if denied.
 */
int policy_is_cmd_allowed(const struct policy *p, const char *command);

/*
 * policy_is_full_profile - returns 1 if profile is "full".
 */
int policy_is_full_profile(const struct policy *p);

/*
 * policy_is_path_in_rw_root - check if canonical path is under a rw_root,
 * passes deny checks, but does NOT check the write extension allowlist.
 * Used for make_directory (no extension) and rename_path destination.
 * Returns 1 if allowed, 0 if denied.
 */
int policy_is_path_in_rw_root(const struct policy *p, const char *path);

#endif /* MCPSERVER_POLICY_H */
