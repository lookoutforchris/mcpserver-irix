/*
 * tools_build.c - build and program execution tools
 *
 * Implements run_build_command and run_program.
 *
 * Unlike run_inspect_command, these tools impose minimal argument
 * restrictions: only shell metacharacters are rejected. Build tools
 * (cc, make, etc.) require full flag freedom that a fixed allowlist
 * cannot accommodate. The working directory and program path are
 * checked against policy roots.
 */

#include "tools_build.h"
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
/* Build tool table                                                     */
/* ------------------------------------------------------------------ */

struct build_tool {
    const char *name;
    const char *bin_path;
};

static const struct build_tool BUILD_TABLE[] = {
    /* C/C++ compilers */
    { "cc",       "/usr/bin/cc"        },
    { "c++",      "/usr/bin/c++"       },
    { "CC",       "/usr/bin/CC"        },

    /* Build system */
    { "make",     "/usr/bin/make"      },
    { "gmake",    "/usr/local/bin/gmake" },  /* if SGUG-RSE present */

    /* Linker and archiver */
    { "ld",       "/usr/bin/ld"        },
    { "ar",       "/usr/bin/ar"        },
    { "ranlib",   "/usr/bin/ranlib"    },

    /* Binary utilities */
    { "strip",    "/usr/bin/strip"     },
    { "elfdump",  "/usr/bin/elfdump"   },
    { "dis",      "/usr/bin/dis"       },

    /* File operations needed by install scripts */
    { "chmod",    "/bin/chmod"         },
    { "chown",    "/bin/chown"         },
    { "cp",       "/bin/cp"            },
    { "mv",       "/bin/mv"            },
    { "ln",       "/bin/ln"            },
    { "mkdir",    "/bin/mkdir"         },
    { "rm",       "/bin/rm"            },
    { "install",  "/usr/bin/install"   },

    /* Archiving and compression */
    { "tar",      "/bin/tar"           },
    { "gzip",     "/usr/bin/gzip"      },
    { "compress", "/usr/bin/compress"  },
    { "uncompress","/usr/bin/uncompress"},

    /* IRIX packaging */
    { "makedist", "/usr/bin/makedist"  },
    { "swpkg",    "/usr/bin/swpkg"     },
    { "inst",     "/usr/sbin/inst"     },
    { "gendist",  "/usr/bin/gendist"   },

    { NULL, NULL }
};

/* ------------------------------------------------------------------ */
/* Argument safety check (metacharacters only — no flag allowlist)     */
/* ------------------------------------------------------------------ */

static const char *BUILD_BAD_CHARS = "|&;><\n\r$`";

static int
build_arg_safe(const char *arg)
{
    const char *p = BUILD_BAD_CHARS;
    while (*p) {
        if (strchr(arg, *p)) return 0;
        p++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Shared output capture (longer timeout, larger buffer)               */
/* ------------------------------------------------------------------ */

static pid_t g_build_child = -1;

static void
build_sigalrm(int sig)
{
    (void)sig;
    if (g_build_child > 0) {
        kill(g_build_child, SIGKILL);
        g_build_child = -1;
    }
}

/*
 * capture_build - fork/exec with optional chdir, capture output.
 * Returns exit code, or -1 on error. Sets *timed_out if killed.
 */
static int
capture_build(const char *bin_path,
              const char **argv,  /* argv[0] = name, NULL-terminated */
              const char *work_dir,
              char *out_buf, int out_bufsz,
              int timeout_sec,
              int *timed_out)
{
    int          pfd[2];
    pid_t        child;
    int          status;
    int          n, total = 0;
    struct sigaction sa, old_sa;

    *timed_out = 0;
    if (pipe(pfd) != 0) return -1;

    child = fork();
    if (child < 0) {
        close(pfd[0]); close(pfd[1]);
        return -1;
    }

    if (child == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        if (work_dir) {
            if (chdir(work_dir) != 0) {
                write(STDERR_FILENO, "chdir failed\n", 13);
                _exit(127);
            }
        }
        execv(bin_path, (char *const *)argv);
        _exit(127);
    }

    close(pfd[1]);
    g_build_child = child;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = build_sigalrm;
    sigaction(SIGALRM, &sa, &old_sa);
    alarm((unsigned int)timeout_sec);

    while (total < out_bufsz - 1) {
        n = (int)read(pfd[0], out_buf + total, (size_t)(out_bufsz - 1 - total));
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        total += n;
    }
    out_buf[total] = '\0';
    close(pfd[0]);

    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);

    if (waitpid(child, &status, 0) < 0) {
        if (errno == ECHILD) { *timed_out = 1; g_build_child = -1; return -1; }
    }
    g_build_child = -1;

    if (*timed_out) return -1;
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) { *timed_out = 1; return -1; }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void
build_args_json(const char **args, int nargs, char *buf, int bufsz)
{
    int    i, pos = 0;
    char   esc[1024];
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

/* ------------------------------------------------------------------ */
/* run_build_command                                                    */
/* ------------------------------------------------------------------ */

int
tool_run_build_command(const struct policy *p,
                       const char *command,
                       const char **args,
                       const char *work_dir,
                       char *resp_buf, int resp_bufsz)
{
    static char out_buf[MCP_BUILD_OUTPUT_MAX + 1];
    static char ecmd[128];
    static char eargs[8192];
    static char ework[1024];
    const char *exec_argv[BUILD_ARGS_MAX + 2];
    int         nargs = 0, i;
    int         exit_code, timed_out = 0;
    int         truncated = 0;
    const struct build_tool *tool = NULL;
    char        eout[MCP_BUILD_OUTPUT_MAX * 2 + 4];

    json_escape(command ? command : "", ecmd, sizeof(ecmd));

    if (args) while (args[nargs] && nargs < BUILD_ARGS_MAX) nargs++;
    build_args_json(args, nargs, eargs, sizeof(eargs));

    if (work_dir) json_escape(work_dir, ework, sizeof(ework));
    else          ework[0] = '\0';

    /* look up in build tool table */
    for (i = 0; BUILD_TABLE[i].name; i++) {
        if (strcmp(BUILD_TABLE[i].name, command) == 0) {
            tool = &BUILD_TABLE[i];
            break;
        }
    }

    if (!tool) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
            "{\"allowed\":false,\"command\":\"%s\",\"args\":%s,"
            "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
            "\"truncated\":false,\"timed_out\":false,"
            "\"error\":\"command not in build tool list\"}",
            ecmd, eargs, ework);
    }

    /* validate work_dir against policy */
    if (work_dir && !policy_is_read_allowed(p, work_dir)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
            "{\"allowed\":false,\"command\":\"%s\",\"args\":%s,"
            "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
            "\"truncated\":false,\"timed_out\":false,"
            "\"error\":\"work_dir outside allowed roots\"}",
            ecmd, eargs, ework);
    }

    /* check args for shell metacharacters */
    for (i = 0; i < nargs; i++) {
        if (!args[i] || !build_arg_safe(args[i])) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                "{\"allowed\":false,\"command\":\"%s\",\"args\":%s,"
                "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
                "\"truncated\":false,\"timed_out\":false,"
                "\"error\":\"argument contains forbidden characters\"}",
                ecmd, eargs, ework);
        }
    }

    /* build exec argv */
    exec_argv[0] = command;
    for (i = 0; i < nargs; i++) exec_argv[i + 1] = args[i];
    exec_argv[nargs + 1] = NULL;

    out_buf[0] = '\0';
    exit_code = capture_build(tool->bin_path,
                              (const char **)exec_argv,
                              work_dir,
                              out_buf, sizeof(out_buf),
                              MCP_BUILD_TIMEOUT_SEC,
                              &timed_out);

    if ((int)strlen(out_buf) >= MCP_BUILD_OUTPUT_MAX) truncated = 1;

    json_escape(out_buf, eout, sizeof(eout));

    return snprintf(resp_buf, (size_t)resp_bufsz,
        "{\"allowed\":true,\"command\":\"%s\",\"args\":%s,"
        "\"work_dir\":\"%s\",\"stdout\":\"%s\",\"exit_code\":%d,"
        "\"truncated\":%s,\"timed_out\":%s}",
        ecmd, eargs, ework, eout, exit_code,
        truncated  ? "true" : "false",
        timed_out  ? "true" : "false");
}

/* ------------------------------------------------------------------ */
/* run_program                                                          */
/* ------------------------------------------------------------------ */

int
tool_run_program(const struct policy *p,
                 const char *program,
                 const char **args,
                 const char *work_dir,
                 char *resp_buf, int resp_bufsz)
{
    static char out_buf[MCP_BUILD_OUTPUT_MAX + 1];
    static char eprog[1024];
    static char eargs[8192];
    static char ework[1024];
    const char *exec_argv[BUILD_ARGS_MAX + 2];
    int         nargs = 0, i;
    int         exit_code, timed_out = 0;
    int         truncated = 0;
    char        eout[MCP_BUILD_OUTPUT_MAX * 2 + 4];

    json_escape(program ? program : "", eprog, sizeof(eprog));

    if (args) while (args[nargs] && nargs < BUILD_ARGS_MAX) nargs++;
    build_args_json(args, nargs, eargs, sizeof(eargs));

    if (work_dir) json_escape(work_dir, ework, sizeof(ework));
    else          ework[0] = '\0';

    if (!program || program[0] == '\0') {
        return snprintf(resp_buf, (size_t)resp_bufsz,
            "{\"allowed\":false,\"program\":\"%s\",\"args\":%s,"
            "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
            "\"truncated\":false,\"timed_out\":false,"
            "\"error\":\"program path is required\"}",
            eprog, eargs, ework);
    }

    /* program must be within a policy root */
    if (!policy_is_read_allowed(p, program)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
            "{\"allowed\":false,\"program\":\"%s\",\"args\":%s,"
            "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
            "\"truncated\":false,\"timed_out\":false,"
            "\"error\":\"program path outside allowed roots\"}",
            eprog, eargs, ework);
    }

    /* work_dir must be within a root if supplied */
    if (work_dir && !policy_is_read_allowed(p, work_dir)) {
        return snprintf(resp_buf, (size_t)resp_bufsz,
            "{\"allowed\":false,\"program\":\"%s\",\"args\":%s,"
            "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
            "\"truncated\":false,\"timed_out\":false,"
            "\"error\":\"work_dir outside allowed roots\"}",
            eprog, eargs, ework);
    }

    /* check args for metacharacters */
    for (i = 0; i < nargs; i++) {
        if (!args[i] || !build_arg_safe(args[i])) {
            return snprintf(resp_buf, (size_t)resp_bufsz,
                "{\"allowed\":false,\"program\":\"%s\",\"args\":%s,"
                "\"work_dir\":\"%s\",\"stdout\":\"\",\"exit_code\":-1,"
                "\"truncated\":false,\"timed_out\":false,"
                "\"error\":\"argument contains forbidden characters\"}",
                eprog, eargs, ework);
        }
    }

    /* argv[0] = basename of program */
    exec_argv[0] = program;
    for (i = 0; i < nargs; i++) exec_argv[i + 1] = args[i];
    exec_argv[nargs + 1] = NULL;

    out_buf[0] = '\0';
    exit_code = capture_build(program,
                              (const char **)exec_argv,
                              work_dir,
                              out_buf, sizeof(out_buf),
                              MCP_BUILD_TIMEOUT_SEC,
                              &timed_out);

    if ((int)strlen(out_buf) >= MCP_BUILD_OUTPUT_MAX) truncated = 1;

    json_escape(out_buf, eout, sizeof(eout));

    return snprintf(resp_buf, (size_t)resp_bufsz,
        "{\"allowed\":true,\"program\":\"%s\",\"args\":%s,"
        "\"work_dir\":\"%s\",\"stdout\":\"%s\",\"exit_code\":%d,"
        "\"truncated\":%s,\"timed_out\":%s}",
        eprog, eargs, ework, eout, exit_code,
        truncated ? "true" : "false",
        timed_out ? "true" : "false");
}
