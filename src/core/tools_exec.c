/*
 * tools_exec.c - constrained command execution tool
 *
 * STUB: argument validation and exec() logic not yet implemented.
 *
 * When implemented:
 *   - command binary resolved from hardcoded table (never PATH)
 *   - each argument validated against allowed flag set per command
 *   - path arguments checked against policy
 *   - executed via execv() (no shell)
 *   - stdout/stderr captured via pipe
 *   - output clipped at MCP_CONTENT_MAX
 *   - process killed after MCP_CMD_TIMEOUT_SEC seconds
 */

#include "tools_exec.h"
#include "result.h"
#include "json.h"

#include <stdio.h>
#include <string.h>

/*
 * Hardcoded command table: maps allowed command names to binary paths.
 * Binary paths are absolute and fixed - never resolved via PATH.
 */
static const struct {
    const char *name;
    const char *path;
} CMD_TABLE[] = {
    { "pwd",  "/bin/pwd"  },
    { "ls",   "/bin/ls"   },
    { "find", "/bin/find" },
    { "cat",  "/bin/cat"  },
    { "grep", "/bin/grep" },
    { "head", "/bin/head" },
    { "tail", "/bin/tail" },
    { "sed",  "/bin/sed"  },
    { "wc",   "/bin/wc"   },
    { "stat", "/bin/stat" },
    { NULL,   NULL        }
};

/*
 * BAD_CHARS - characters rejected anywhere in any argument.
 * Prevents shell metacharacter injection even though we don't use a shell,
 * as defence in depth.
 */
static const char BAD_CHARS[] = "| & ; > < \n\r\0 $ ` ( )";

static int
arg_is_safe(const char *arg)
{
    const char *p;
    for (p = arg; *p; p++) {
        if (*p == '|' || *p == '&' || *p == ';' ||
            *p == '>' || *p == '<' || *p == '\n' ||
            *p == '\r' || *p == '$' || *p == '`' ||
            *p == '(' || *p == ')')
            return 0;
    }
    return 1;
}

int
tool_run_inspect_command(const struct policy *p,
                         const char *command,
                         const char **args,
                         char *resp_buf, int resp_bufsz)
{
    int i;
    (void)p; (void)args;

    /* validate command is in table */
    for (i = 0; CMD_TABLE[i].name; i++) {
        if (strcmp(CMD_TABLE[i].name, command) == 0)
            break;
    }
    if (!CMD_TABLE[i].name) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"command\":\"%s\","
                        "\"args\":[],\"stdout\":\"\",\"stderr\":\"\","
                        "\"exit_code\":-1,\"truncated\":false,"
                        "\"error\":\"command not allowed\"}",
                        command);
    }

    /*
     * TODO: validate per-command argument set against allowed flags.
     * TODO: check path args against policy.
     * TODO: fork(), execv(), capture output via pipe, apply timeout.
     */

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"command\":\"%s\","
                    "\"args\":[],\"stdout\":\"\",\"stderr\":\"\","
                    "\"exit_code\":0,\"truncated\":false,"
                    "\"error\":\"not yet implemented\"}",
                    command);
}
