/*
 * fnmatch.c - portable glob pattern matching
 *
 * Implements mcp_fnmatch() for IRIX targets where fnmatch(3) is absent
 * or inconsistent. Used by the boundary policy engine for deny glob rules.
 *
 * Supports: * ? ** [...]
 *   *   any sequence of non-'/' characters (with MCP_FNM_PATHNAME)
 *   **  any sequence of characters including '/' (recursive glob)
 *   ?   any single non-'/' character
 *   [...]  character class
 */

#include "compat.h"
#include <string.h>

/*
 * match_class - match a character class [...] at pat against ch.
 * Returns pointer past the closing ']' on match, NULL on no match or error.
 */
static const char *
match_class(const char *pat, int ch)
{
    int negate = 0;
    int matched = 0;
    int first = 1;

    if (*pat == '!') {
        negate = 1;
        pat++;
    }

    while (*pat && (*pat != ']' || first)) {
        first = 0;
        if (*pat == '\\' && *(pat + 1)) {
            pat++;
            if (*pat == ch)
                matched = 1;
            pat++;
        } else if (*(pat + 1) == '-' && *(pat + 2) && *(pat + 2) != ']') {
            if ((unsigned char)ch >= (unsigned char)*pat &&
                (unsigned char)ch <= (unsigned char)*(pat + 2))
                matched = 1;
            pat += 3;
        } else {
            if (*pat == ch)
                matched = 1;
            pat++;
        }
    }

    if (*pat != ']')
        return NULL; /* malformed class */

    return (matched != negate) ? pat + 1 : NULL;
}

/*
 * fnmatch_impl - recursive implementation.
 * Both pat and str are walked simultaneously.
 */
static int
fnmatch_impl(const char *pat, const char *str, int flags)
{
    int path = (flags & MCP_FNM_PATHNAME) != 0;

    while (*pat) {
        if (*pat == '*') {
            /*
             * Check for ** (recursive glob).
             * ** matches any sequence including '/'.
             * * matches any sequence except '/'.
             */
            if (*(pat + 1) == '*') {
                /* skip all consecutive *'s and **'s */
                pat += 2;
                while (*pat == '*')
                    pat++;

                /* ** at end matches everything */
                if (*pat == '\0')
                    return 0;

                /* try matching ** against each suffix of str */
                while (*str) {
                    if (fnmatch_impl(pat, str, flags) == 0)
                        return 0;
                    str++;
                }
                return fnmatch_impl(pat, str, flags);
            } else {
                /* single * - does not cross '/' in pathname mode */
                pat++;
                /* try matching * against each non-'/' suffix */
                while (*str) {
                    if (path && *str == '/')
                        break;
                    if (fnmatch_impl(pat, str, flags) == 0)
                        return 0;
                    str++;
                }
                return fnmatch_impl(pat, str, flags);
            }
        }

        if (*pat == '?') {
            if (*str == '\0')
                return MCP_FNM_NOMATCH;
            if (path && *str == '/')
                return MCP_FNM_NOMATCH;
            pat++;
            str++;
            continue;
        }

        if (*pat == '[') {
            const char *next;
            if (*str == '\0')
                return MCP_FNM_NOMATCH;
            if (path && *str == '/')
                return MCP_FNM_NOMATCH;
            next = match_class(pat + 1, (unsigned char)*str);
            if (!next)
                return MCP_FNM_NOMATCH;
            pat = next;
            str++;
            continue;
        }

        if (*pat == '\\' && !(flags & MCP_FNM_NOESCAPE)) {
            pat++;
            if (*pat == '\0')
                return MCP_FNM_NOMATCH;
        }

        if (*pat != *str)
            return MCP_FNM_NOMATCH;

        pat++;
        str++;
    }

    return (*str == '\0') ? 0 : MCP_FNM_NOMATCH;
}

int
mcp_fnmatch(const char *pattern, const char *string, int flags)
{
    if (!pattern || !string)
        return MCP_FNM_NOMATCH;
    return fnmatch_impl(pattern, string, flags | MCP_FNM_PATHNAME);
}
