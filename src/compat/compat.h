/*
 * compat.h - Portable type definitions and platform shims
 *
 * Provides int8_t/uint32_t etc. for IRIX 5.3 (no stdint.h), and declarations
 * for compat implementations of realpath(3) and fnmatch(3) which are absent
 * or unreliable on older IRIX targets.
 *
 * Include this header before any other project header.
 */

#ifndef MCPSERVER_COMPAT_H
#define MCPSERVER_COMPAT_H

#include <sys/types.h>  /* size_t, ssize_t, pid_t */

/*
 * Integer types.
 * sgidefs.h provides _MIPS_SZINT, _MIPS_SZLONG, _MIPS_SZPTR on MIPSpro.
 * The ucode compiler on IRIX 5.3 may not have sgidefs.h; fall back to
 * the known sizes for O32 (int=32, long=32, ptr=32).
 */
typedef signed   char        int8_t;
typedef unsigned char        uint8_t;
typedef signed   short       int16_t;
typedef unsigned short       uint16_t;
typedef signed   int         int32_t;
typedef unsigned int         uint32_t;
typedef signed   long long   int64_t;
typedef unsigned long long   uint64_t;

/*
 * Size limits used throughout the project.
 */
#define MCPSERVER_PATH_MAX   1024   /* max path length in any API */
#define MCPSERVER_VERSION    "0.1.0"

/*
 * mcp_realpath - portable path canonicalization.
 *
 * Resolves all . and .. components and follows symlinks without calling
 * the libc realpath(3), which is absent on IRIX 5.3.
 *
 * resolved must point to a buffer of at least MCPSERVER_PATH_MAX bytes.
 * Returns resolved on success, NULL on error (sets errno).
 */
char *mcp_realpath(const char *path, char *resolved);

/*
 * mcp_fnmatch - portable glob pattern matching.
 *
 * Supports: * ? ** [...]
 *   *   matches any sequence of characters except '/'
 *   **  matches any sequence of characters including '/'
 *   ?   matches any single character except '/'
 *   [...] character class
 *
 * Returns 0 if pattern matches string, MCP_FNM_NOMATCH otherwise.
 */
int mcp_fnmatch(const char *pattern, const char *string, int flags);

#define MCP_FNM_NOMATCH   1     /* pattern did not match */
#define MCP_FNM_PATHNAME  0x01  /* * does not match / */
#define MCP_FNM_NOESCAPE  0x02  /* backslash is not an escape */

#endif /* MCPSERVER_COMPAT_H */
