/*
 * compat.h - Portable type definitions and platform shims
 *
 * Provides int8_t/uint32_t etc. and declarations for mcp_realpath()
 * and mcp_fnmatch(), which replace libc functions absent on IRIX 5.3.
 *
 * Integer type strategy:
 *   MIPSpro (_COMPILER_VERSION defined): <inttypes.h> is available on
 *   IRIX 6.x and provides all intN_t types. Use it to avoid cc-1275
 *   "typedef already declared" warnings under -ansi -fullwarn.
 *
 *   IDO ucode compiler (IRIX 5.3, no _COMPILER_VERSION): define types
 *   manually. int64_t/uint64_t are omitted because long long is not
 *   available in strict C89 on the IDO compiler.
 *
 * Include this header before any other project header.
 */

#ifndef MCPSERVER_COMPAT_H
#define MCPSERVER_COMPAT_H

#include <sys/types.h>  /* size_t, ssize_t, pid_t */

#if defined(_COMPILER_VERSION)
/*
 * MIPSpro on IRIX 6.x. <sys/types.h> already defines int8_t through
 * uint32_t. <inttypes.h> provides the full set including int64_t.
 * Using it avoids cc-1275 redefinition warnings.
 */
#include <inttypes.h>

#else
/*
 * IDO ucode compiler on IRIX 5.3.
 * Define the subset we need manually. long long is a C99 extension not
 * guaranteed on IDO; int64_t/uint64_t are intentionally omitted here.
 */
typedef signed   char        int8_t;
typedef unsigned char        uint8_t;
typedef signed   short       int16_t;
typedef unsigned short       uint16_t;
typedef signed   int         int32_t;
typedef unsigned int         uint32_t;

#endif /* _COMPILER_VERSION */

/*
 * Size limits used throughout the project.
 */
#define MCPSERVER_PATH_MAX   1024   /* max path length in any API */
#define MCPSERVER_VERSION    "0.1.0"

/*
 * mcp_realpath - portable path canonicalization.
 *
 * Resolves all . and .. components and follows symlinks without calling
 * libc realpath(3), which is absent on IRIX 5.3.
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
