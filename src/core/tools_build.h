/*
 * tools_build.h - build and program execution tools
 *
 * Implements:
 *   run_build_command - run a build tool (cc, make, ar, tar, etc.)
 *   run_program       - run a compiled executable within a policy root
 *
 * Security model: arguments are checked for shell metacharacters only.
 * No per-command flag allowlist — build tools require full flag freedom.
 * The working directory (if supplied) must be within a configured project
 * root. For run_program, the executable path must also be within a root.
 *
 * Timeout: MCP_BUILD_TIMEOUT_SEC (longer than inspect commands).
 * Output:  MCP_BUILD_OUTPUT_MAX bytes.
 *
 * Full profile only.
 */

#ifndef MCPSERVER_TOOLS_BUILD_H
#define MCPSERVER_TOOLS_BUILD_H

#include "policy.h"

#define BUILD_ARGS_MAX       64
#define MCP_BUILD_TIMEOUT_SEC 300   /* 5 minutes */
#define MCP_BUILD_OUTPUT_MAX  65536 /* 64 KB */

/*
 * tool_run_build_command - run a build/compile/package tool.
 *
 * command:  tool name — must be in the build tool table (cc, make, etc.)
 * args:     NULL-terminated argument array
 * work_dir: working directory (NULL = daemon cwd). Must be within a root
 *           if non-NULL. Typically the project source root.
 */
int tool_run_build_command(const struct policy *p,
                           const char *command,
                           const char **args,
                           const char *work_dir,
                           char *resp_buf, int resp_bufsz);

/*
 * tool_run_program - execute a compiled program within a policy root.
 *
 * program: absolute path to the executable. Must be within a policy root.
 * args:    NULL-terminated argument array.
 * work_dir: working directory (NULL = daemon cwd). If non-NULL must be
 *           within a policy root.
 */
int tool_run_program(const struct policy *p,
                     const char *program,
                     const char **args,
                     const char *work_dir,
                     char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_TOOLS_BUILD_H */
