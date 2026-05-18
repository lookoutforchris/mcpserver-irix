/*
 * tools_write.h - file write and path management tools
 *
 * Implements: create_text_file, replace_text_file, make_directory,
 *             delete_text_file, rename_path
 *
 * All functions are only accessible in the "full" profile.
 * Callers must verify policy_is_full_profile() before calling.
 */

#ifndef MCPSERVER_TOOLS_WRITE_H
#define MCPSERVER_TOOLS_WRITE_H

#include "policy.h"

int tool_create_text_file(const struct policy *p,
                          const char *path, const char *content,
                          char *resp_buf, int resp_bufsz);

int tool_replace_text_file(const struct policy *p,
                           const char *path, const char *content,
                           char *resp_buf, int resp_bufsz);

int tool_make_directory(const struct policy *p, const char *path,
                        char *resp_buf, int resp_bufsz);

int tool_delete_text_file(const struct policy *p, const char *path,
                          char *resp_buf, int resp_bufsz);

int tool_rename_path(const struct policy *p,
                     const char *source, const char *dest,
                     char *resp_buf, int resp_bufsz);

#endif /* MCPSERVER_TOOLS_WRITE_H */
