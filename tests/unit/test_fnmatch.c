/*
 * test_fnmatch.c - unit tests for mcp_fnmatch()
 *
 * Compile and run on any POSIX host (including the dev workstation)
 * to verify the portable fnmatch implementation before IRIX testing.
 *
 * Build: cc -ansi -o test_fnmatch test_fnmatch.c ../../src/compat/fnmatch.c
 * Run:   ./test_fnmatch
 */

#include "../../src/compat/compat.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void
check(const char *pattern, const char *string, int expect_match, const char *desc)
{
    int result = mcp_fnmatch(pattern, string, 0);
    int matched = (result == 0);

    if (matched != expect_match) {
        printf("FAIL: %s\n", desc);
        printf("      pattern=\"%s\" string=\"%s\"\n", pattern, string);
        printf("      expected %s, got %s\n",
               expect_match ? "MATCH" : "NOMATCH",
               matched      ? "MATCH" : "NOMATCH");
        failures++;
    } else {
        printf("ok:   %s\n", desc);
    }
}

int
main(void)
{
    /* basic exact match */
    check("foo",        "foo",              1, "exact match");
    check("foo",        "bar",              0, "exact no match");

    /* single * (no /) */
    check("*.c",        "main.c",           1, "*.c matches main.c");
    check("*.c",        "foo/main.c",       0, "*.c does not cross /");
    check("*.c",        "main.h",           0, "*.c does not match .h");

    /* double ** (crosses /) */
    check("**/.env",    "/work/.env",       1, "**/.env matches top");
    check("**/.env",    "/work/sub/.env",   1, "**/.env matches deep");
    check("**/.env",    "/work/.env.local", 0, "**/.env no partial");

    /* ? wildcard */
    check("foo?",       "foob",             1, "? matches single char");
    check("foo?",       "foo",              0, "? requires a char");
    check("foo?",       "foo/b",            0, "? does not match /");

    /* ** at end */
    check("src/**",     "src/main.c",       1, "src/** matches file");
    check("src/**",     "src/a/b/c.h",      1, "src/** matches deep");
    check("src/**",     "other/main.c",     0, "src/** no other prefix");

    /* deny glob patterns from SECURITY_MODEL.md */
    check("**/.env.*",  "/a/b/.env.local",  1, "deny .env.local");
    check("**/*.secret","/a/creds.secret",  1, "deny .secret");
    check("**/*.key",   "/etc/ssh/id_rsa.key", 1, "deny .key");

    /* character class */
    check("[abc]",      "a",                1, "class match a");
    check("[abc]",      "d",                0, "class no match d");
    check("[a-z]",      "m",                1, "range match");
    check("[!abc]",     "d",                1, "negated class");
    check("[!abc]",     "a",                0, "negated class no match");

    printf("\n%d test(s) failed.\n", failures);
    return failures > 0 ? 1 : 0;
}
