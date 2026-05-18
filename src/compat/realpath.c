/*
 * realpath.c - portable path canonicalization
 *
 * Implements mcp_realpath() without calling libc realpath(3), which is
 * absent on IRIX 5.3. Resolves . and .. components and follows symlinks
 * using stat(2) and readlink(2), both confirmed available on IRIX 5.0+.
 *
 * The resolved buffer must be at least MCPSERVER_PATH_MAX bytes.
 */

#include "compat.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define SYMLOOP_MAX 32  /* max symlink hops before ELOOP */

/*
 * resolve_path - walk path component by component into out[].
 * out must be MCPSERVER_PATH_MAX bytes. Returns 0 on success, -1 on error.
 */
static int
resolve_path(const char *path, char *out, int depth)
{
    char         component[MCPSERVER_PATH_MAX];
    char         linkbuf[MCPSERVER_PATH_MAX];
    const char  *p;
    const char  *end;
    char        *outp;
    size_t       outlen;
    struct stat  st;
    ssize_t      llen;
    int          n;

    if (depth > SYMLOOP_MAX) {
        errno = ELOOP;
        return -1;
    }

    if (!path || path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    /* start output */
    if (path[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        p = path + 1;
    } else {
        /* relative path: start from cwd */
        if (getcwd(out, MCPSERVER_PATH_MAX) == NULL)
            return -1;
        p = path;
    }

    while (*p) {
        /* skip consecutive slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* find end of this component */
        end = p;
        while (*end && *end != '/') end++;

        n = (int)(end - p);
        if (n == 0) break;

        /* handle . */
        if (n == 1 && p[0] == '.') {
            p = end;
            continue;
        }

        /* handle .. */
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            /* strip last component from out */
            outp = strrchr(out, '/');
            if (outp && outp != out)
                *outp = '\0';
            else
                out[1] = '\0'; /* back to root */
            p = end;
            continue;
        }

        /* copy component */
        if (n >= MCPSERVER_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(component, p, (size_t)n);
        component[n] = '\0';

        /* append to out */
        outlen = strlen(out);
        if (outlen + 1 + (size_t)n + 1 >= MCPSERVER_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (out[outlen - 1] != '/')
            out[outlen++] = '/';
        memcpy(out + outlen, component, (size_t)n + 1);

        /* check for symlink */
        if (lstat(out, &st) == 0 && S_ISLNK(st.st_mode)) {
            llen = readlink(out, linkbuf, MCPSERVER_PATH_MAX - 1);
            if (llen < 0)
                return -1;
            linkbuf[llen] = '\0';

            /* strip the symlink component from out before recursing */
            outp = strrchr(out, '/');
            if (outp && outp != out)
                *outp = '\0';
            else
                out[1] = '\0';

            /* if symlink is relative, prepend current out */
            if (linkbuf[0] != '/') {
                char tmp[MCPSERVER_PATH_MAX];
                outlen = strlen(out);
                if (outlen + 1 + (size_t)llen + 1 >= MCPSERVER_PATH_MAX) {
                    errno = ENAMETOOLONG;
                    return -1;
                }
                memcpy(tmp, out, outlen);
                tmp[outlen] = '/';
                memcpy(tmp + outlen + 1, linkbuf, (size_t)llen + 1);
                if (resolve_path(tmp, out, depth + 1) != 0)
                    return -1;
            } else {
                if (resolve_path(linkbuf, out, depth + 1) != 0)
                    return -1;
            }

            /* append remaining path after the symlink component */
            if (*end) {
                outlen = strlen(out);
                if (outlen + strlen(end) + 1 >= MCPSERVER_PATH_MAX) {
                    errno = ENAMETOOLONG;
                    return -1;
                }
                if (out[outlen - 1] != '/')
                    out[outlen++] = '/';
                /* end points at '/' or next component */
                while (*end == '/') end++;
                strcpy(out + outlen, end);
                return resolve_path(out, out, depth + 1);
            }
        }

        p = end;
    }

    /* clean trailing slash (except root) */
    outlen = strlen(out);
    if (outlen > 1 && out[outlen - 1] == '/')
        out[outlen - 1] = '\0';

    return 0;
}

char *
mcp_realpath(const char *path, char *resolved)
{
    char tmp[MCPSERVER_PATH_MAX];

    if (!resolved) {
        errno = EINVAL;
        return NULL;
    }

    tmp[0] = '\0';
    if (resolve_path(path, tmp, 0) != 0)
        return NULL;

    memcpy(resolved, tmp, strlen(tmp) + 1);
    return resolved;
}
