/*
 * compat.h - Portable type definitions and platform shims
 *
 * Provides int8_t/uint32_t etc. and declarations for mcp_realpath()
 * and mcp_fnmatch(), which replace libc functions absent on IRIX 5.3.
 *
 * Integer type strategy — three cases:
 *
 *   1. MIPSpro N32/N64 (_COMPILER_VERSION defined):
 *      <inttypes.h> is available and provides all intN_t types. Include
 *      it to avoid cc-1275 redefinition warnings under -ansi -fullwarn.
 *
 *   2. MIPSpro O32 cross-compile on IRIX 6.x (-o32 -ansi suppresses
 *      _COMPILER_VERSION, but _MIPS_SIM is still defined):
 *      <sys/types.h> on IRIX 6.x already defines int8_t through uint32_t.
 *      No additional typedefs are needed; defining them here causes errors.
 *
 *   3. IDO ucode compiler on IRIX 5.3 (neither _COMPILER_VERSION nor
 *      _MIPS_SIM is defined):
 *      <sys/types.h> on IRIX 5.3 does not provide intN_t types. Define
 *      them manually. int64_t/uint64_t are omitted — long long is not
 *      available in strict C89 on the IDO compiler.
 *
 * Include this header before any other project header.
 */

#ifndef MCPSERVER_COMPAT_H
#define MCPSERVER_COMPAT_H

#include <sys/types.h>  /* size_t, ssize_t, pid_t */

#if defined(_COMPILER_VERSION)
/* Case 1: MIPSpro N32/N64 on IRIX 6.x */
#include <inttypes.h>

#elif defined(_MIPS_SIM)
/* Case 2: MIPSpro O32 cross-compile on IRIX 6.x.
 * <sys/types.h> already provides int8_t...uint32_t. */

#else
/* Case 3: IDO ucode compiler on IRIX 5.3. */
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
#define MCPSERVER_VERSION    "0.3.0"

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
