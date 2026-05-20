/*
 * tools_exec.c - constrained command execution tool
 *
 * Implements run_inspect_command.
 *
 * Security model (docs/SECURITY_MODEL.md §4):
 *   - Command binary resolved from hardcoded table, never PATH.
 *   - Per-command allowed-flag set enforced before exec.
 *   - All path arguments checked against policy.
 *   - No shell. execv() only.
 *   - Output captured via pipe, clipped at MCP_CONTENT_MAX.
 *   - SIGALRM timeout of MCP_CMD_TIMEOUT_SEC seconds.
 */

#include "tools_exec.h"
#include "result.h"
#include "json.h"
#include "../compat/compat.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Command table and per-command validation                             */
/* ------------------------------------------------------------------ */

/*
 * Each allowed command has a binary path and a set of allowed flags.
 * Flags are short strings that may appear as arguments.
 * Path arguments (not starting with -) are checked against policy.
 */

#define CMD_FLAGS_MAX  16
#define CMD_FLAG_LEN   8

struct cmd_def {
    const char *name;
    const char *bin_path;
    /* NULL-terminated list of allowed flag strings */
    const char *flags[CMD_FLAGS_MAX];
    /* 1 if path args (non-flag args) are allowed; 0 if command takes no paths */
    int         allow_path_args;
    /* 1 if -n NUM style numeric option is allowed (head/tail/wc) */
    int         allow_num_opt;
};

static const struct cmd_def CMD_TABLE[] = {
    { "pwd",  "/bin/pwd",
      { NULL },
      0, 0 },

    { "ls",   "/bin/ls",
      { "-1", "-a", "-l", "-h", "-d", NULL },
      1, 0 },

    { "find", "/bin/find",
      { "-maxdepth", "-mindepth", "-type", "-name", "-iname",
        "-print", "-f", "-d", NULL },
      1, 1 },

    { "cat",  "/bin/cat",
      { NULL },
      1, 0 },

    { "grep", "/bin/grep",
      { "-n", "-i", "-F", "-E", "-l", NULL },
      1, 0 },

    { "head", "/bin/head",
      { "-n", NULL },
      1, 1 },

    { "tail", "/bin/tail",
      { "-n", NULL },
      1, 1 },

    { "sed",  "/bin/sed",
      { "-n", NULL },
      1, 0 },

    { "wc",   "/bin/wc",
      { "-l", "-c", "-w", "-m", NULL },
      1, 0 },

    { "stat", "/bin/stat",
      { NULL },
      1, 0 },

    { "diff",    "/usr/bin/diff",
      { "-u", "-c", "-i", "-w", "-b", "-r", "-q", NULL },
      1, 0 },

    { "nm",      "/usr/bin/nm",
      { "-p", "-u", "-g", "-n", "-v", NULL },
      1, 0 },

    { "file",    "/usr/bin/file",
      { "-b", NULL },
      1, 0 },

    { "size",    "/usr/bin/size",
      { NULL },
      1, 0 },

    { "strings", "/usr/bin/strings",
      { "-n", NULL },
      1, 1 },

    { "uname",   "/bin/uname",
      { "-a", "-s", "-r", "-m", "-p", "-n", "-v", NULL },
      0, 0 },

    { "ps",      "/sbin/ps",
      { "-e", "-a", "-f", "-l", "-u", NULL },
      0, 0 },

    { "df",      "/bin/df",
      { "-k", "-l", "-t", NULL },
      0, 0 },

    { NULL, NULL, { NULL }, 0, 0 }
};

/* Characters that must never appear in any argument */
static const char *BAD_CHARS = "|&;><\n\r$`()";

/*
 * build_args_json - serialise args array as a JSON string array.
 * Writes e.g. ["arg1","arg2"] into buf.
 */
static void
build_args_json(const char **args, int nargs, char *buf, int bufsz)
{
    int    i;
    int    pos = 0;
    char   esc[512];
    size_t elen;

    buf[pos++] = '[';
    for (i = 0; i < nargs && pos < bufsz - 4; i++) {
        if (i > 0) buf[pos++] = ',';
        buf[pos++] = '"';
        json_escape(args[i] ? args[i] : "", esc, sizeof(esc));
        elen = strlen(esc);
        if (pos + (int)elen + 2 >= bufsz) break;
        memcpy(buf + pos, esc, elen);
        pos += (int)elen;
        buf[pos++] = '"';
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';
}

static int
arg_is_safe(const char *arg)
{
    const char *p = BAD_CHARS;
    while (*p) {
        if (strchr(arg, *p)) return 0;
        p++;
    }
    return 1;
}

static int
is_flag(const char *arg)
{
    return arg[0] == '-' && arg[1] != '\0';
}

static int
flag_allowed(const struct cmd_def *def, const char *flag)
{
    int i;
    for (i = 0; def->flags[i]; i++) {
        if (strcmp(def->flags[i], flag) == 0) return 1;
    }
    return 0;
}

static int
is_numeric_string(const char *s)
{
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

/*
 * validate_args - check every argument against the per-command rules.
 * Returns 1 if valid, 0 if rejected. Sets *err_msg to a description.
 */
static int
validate_args(const struct cmd_def *def,
              const struct policy *p,
              const char **args, int nargs,
              const char **err_msg)
{
    int i;
    int expect_num = 0; /* next arg should be a number (after -n, -maxdepth, etc.) */
    int expect_str = 0; /* next arg should be a string value (-name pattern, etc.) */

    for (i = 0; i < nargs; i++) {
        if (!args[i] || !arg_is_safe(args[i])) {
            *err_msg = "argument contains forbidden characters";
            return 0;
        }

        if (expect_num) {
            if (!is_numeric_string(args[i])) {
                *err_msg = "expected numeric argument after flag";
                return 0;
            }
            expect_num = 0;
            continue;
        }

        if (expect_str) {
            /* string value after a flag like -name or -type */
            expect_str = 0;
            continue;
        }

        if (is_flag(args[i])) {
            if (!flag_allowed(def, args[i])) {
                *err_msg = "flag not allowed for this command";
                return 0;
            }
            /* flags that take a value */
            if (strcmp(args[i], "-n")        == 0 ||
                strcmp(args[i], "-maxdepth") == 0 ||
                strcmp(args[i], "-mindepth") == 0) {
                expect_num = 1;
            }
            if (strcmp(args[i], "-name")  == 0 ||
                strcmp(args[i], "-iname") == 0 ||
                strcmp(args[i], "-type")  == 0 ||
                strcmp(args[i], "-u")     == 0) {
                expect_str = 1;
            }
        } else {
            /* non-flag: must be a path argument if command allows them */
            if (!def->allow_path_args) {
                *err_msg = "command does not accept path arguments";
                return 0;
            }
            if (!policy_is_read_allowed(p, args[i])) {
                *err_msg = "path argument outside allowed roots";
                return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Output capture via pipe                                              */
/* ------------------------------------------------------------------ */

static pid_t g_exec_child = -1; /* for SIGALRM handler */

static void
sigalrm_handler(int sig)
{
    (void)sig;
    if (g_exec_child > 0) {
        kill(g_exec_child, SIGKILL);
        g_exec_child = -1;
    }
}

/*
 * capture_command - fork, exec, capture stdout+stderr, apply timeout.
 * Returns exit code (0-255), or -1 on fork/exec failure.
 * Output written into out_buf (up to out_bufsz-1 bytes), null-terminated.
 * Sets *timed_out to 1 if killed by SIGALRM.
 */
static int
capture_command(const char *bin_path,
                const char **args_with_cmd, /* args_with_cmd[0] = binary name */
                char *out_buf, int out_bufsz,
                int *timed_out)
{
    int         pipe_fd[2];
    pid_t       child;
    int         status;
    int         n, total = 0;
    struct sigaction sa, old_sa;

    *timed_out = 0;

    if (pipe(pipe_fd) != 0) return -1;

    child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }

    if (child == 0) {
        /* child: redirect stdout and stderr to write end of pipe */
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        execv(bin_path, (char *const *)args_with_cmd);
        _exit(127);
    }

    /* parent */
    close(pipe_fd[1]);
    g_exec_child = child;

    /* install SIGALRM for timeout */
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, &old_sa);
    alarm((unsigned int)MCP_CMD_TIMEOUT_SEC);

    /* read output */
    while (total < out_bufsz - 1) {
        n = (int)read(pipe_fd[0], out_buf + total, (size_t)(out_bufsz - 1 - total));
        if (n < 0) {
            if (errno == EINTR) continue; /* SIGALRM interrupted read */
            break;
        }
        if (n == 0) break;
        total += n;
    }
    out_buf[total] = '\0';
    close(pipe_fd[0]);

    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);

    /* wait for child */
    if (waitpid(child, &status, 0) < 0) {
        if (errno == ECHILD) {
            /* child was killed by SIGALRM handler */
            *timed_out = 1;
            g_exec_child = -1;
            return -1;
        }
    }

    g_exec_child = -1;

    if (*timed_out) return -1;

    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) { *timed_out = 1; return -1; }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int
tool_run_inspect_command(const struct policy *p,
                         const char *command,
                         const char **args,
                         char *resp_buf, int resp_bufsz)
{
    static char out_buf[MCP_CONTENT_MAX + 1];
    static char eout[MCP_CONTENT_MAX * 2 + 1];
    static char ecmd[64];
    static char eargs[4096];
    const char *err_msg = NULL;
    const char *exec_argv[EXEC_ARGS_MAX + 2]; /* +2: name + NULL */
    int         nargs = 0;
    int         i;
    int         exit_code;
    int         timed_out = 0;
    int         truncated = 0;
    const struct cmd_def *def = NULL;

    json_escape(command, ecmd, sizeof(ecmd));

    /* count args and build JSON representation up front */
    if (args) {
        while (args[nargs] && nargs < EXEC_ARGS_MAX) nargs++;
    }
    build_args_json(args, nargs, eargs, sizeof(eargs));

    /* look up command in table */
    for (i = 0; CMD_TABLE[i].name; i++) {
        if (strcmp(CMD_TABLE[i].name, command) == 0) {
            def = &CMD_TABLE[i];
            break;
        }
    }

    if (!def) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"command\":\"%s\","
                        "\"args\":%s,\"stdout\":\"\",\"stderr\":\"\","
                        "\"exit_code\":-1,\"truncated\":false,"
                        "\"error\":\"command not in allowed list\"}",
                        ecmd, eargs);
    }

    /* validate arguments */
    if (!validate_args(def, p, args, nargs, &err_msg)) {
        char eerr[128];
        json_escape(err_msg, eerr, sizeof(eerr));
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":false,\"command\":\"%s\","
                        "\"args\":%s,\"stdout\":\"\",\"stderr\":\"\","
                        "\"exit_code\":-1,\"truncated\":false,"
                        "\"error\":\"%s\"}", ecmd, eargs, eerr);
    }

    /* build exec argv: argv[0] = command name, then args, then NULL */
    exec_argv[0] = command;
    for (i = 0; i < nargs; i++) exec_argv[i + 1] = args[i];
    exec_argv[nargs + 1] = NULL;

    out_buf[0] = '\0';
    exit_code = capture_command(def->bin_path,
                                (const char **)exec_argv,
                                out_buf, (int)sizeof(out_buf),
                                &timed_out);

    if ((int)strlen(out_buf) >= MCP_CONTENT_MAX) truncated = 1;

    json_escape(out_buf, eout, sizeof(eout));

    if (timed_out) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"command\":\"%s\","
                        "\"args\":%s,\"stdout\":\"%s\",\"stderr\":\"\","
                        "\"exit_code\":-1,\"truncated\":%s,"
                        "\"error\":\"command timed out\"}",
                        ecmd, eargs, eout, truncated ? "true" : "false");
    }

    if (exit_code < 0) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
                        "{\"allowed\":true,\"command\":\"%s\","
                        "\"args\":%s,\"stdout\":\"\",\"stderr\":\"\","
                        "\"exit_code\":-1,\"truncated\":false,"
                        "\"error\":\"exec failed\"}", ecmd, eargs);
    }

    return snprintf(resp_buf, (size_t)resp_bufsz,
                    "{\"allowed\":true,\"command\":\"%s\","
                    "\"args\":%s,\"stdout\":\"%s\",\"stderr\":\"\","
                    "\"exit_code\":%d,\"truncated\":%s,"
                    "\"error\":null}",
                    ecmd, eargs, eout, exit_code,
                    truncated ? "true" : "false");
}
