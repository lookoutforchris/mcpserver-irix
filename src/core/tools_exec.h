/*
 * tools_exec.h - constrained command execution tool
 *
 * Implements: run_inspect_command
 *
 * Executes a single whitelisted command via execv() with validated arguments.
 * Never uses a shell. All path arguments are checked against policy.
 * Output is clipped at MCP_CONTENT_MAX. Commands are killed after timeout.
 *
 * Full profile only.
 */

#ifndef MCPSERVER_TOOLS_EXEC_H
#define MCPSERVER_TOOLS_EXEC_H

#include "policy.h"

#define EXEC_ARGS_MAX 32  /* max arguments to any command */

/*
 * tool_run_inspect_command - execute one allowed command.
 *
 * command: command name (e.g. "ls") - looked up in hardcoded table
 * args: NULL-terminated array of argument strings
 */
int tool_run_inspect_command(const struct policy *p,
                             const char *command,
                             const char **args,
                             char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_TOOLS_EXEC_H */
