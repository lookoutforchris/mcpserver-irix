/*
 * test_json.c - unit tests for JSON-RPC parsing and serialisation
 *
 * Build: cc -ansi -Isrc -o test_json test_json.c src/core/json.c
 * Run:   ./test_json
 */

#include "../../src/core/json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

static void
check_str(const char *got, const char *expect, const char *desc)
{
    if (strcmp(got, expect) != 0) {
        printf("FAIL: %s\n  got:    \"%s\"\n  expect: \"%s\"\n", desc, got, expect);
        failures++;
    } else {
        printf("ok:   %s\n", desc);
    }
}

static void
check_int(int got, int expect, const char *desc)
{
    if (got != expect) {
        printf("FAIL: %s  got=%d expect=%d\n", desc, got, expect);
        failures++;
    } else {
        printf("ok:   %s\n", desc);
    }
}

int
main(void)
{
    struct jsonrpc_request req;
    char out[256];
    char resp[1024];
    int  n;
    int  v;

    /* parse a basic request */
    n = jsonrpc_parse(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{}}",
        &req);
    check_int(n, 0, "parse returns 0 on success");
    check_str(req.method, "ping", "method extracted");
    check_str(req.id, "1", "numeric id extracted");
    check_int(req.id_is_null, 0, "id_is_null false for numeric id");

    /* parse with string id */
    jsonrpc_parse(
        "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"tools/list\","
        "\"params\":{}}",
        &req);
    check_str(req.method, "tools/list", "method tools/list");
    check_str(req.id, "abc", "string id extracted");

    /* json_get_string */
    n = json_get_string("{\"name\":\"foo\",\"val\":\"bar\"}", "name", out, sizeof(out));
    check_int(n, 0, "json_get_string returns 0");
    check_str(out, "foo", "json_get_string value");

    /* json_get_int */
    n = json_get_int("{\"count\":42}", "count", &v);
    check_int(n, 0, "json_get_int returns 0");
    check_int(v, 42, "json_get_int value");

    /* json_get_bool */
    n = json_get_bool("{\"flag\":true}", "flag", &v);
    check_int(n, 0, "json_get_bool returns 0");
    check_int(v, 1, "json_get_bool true = 1");

    n = json_get_bool("{\"flag\":false}", "flag", &v);
    check_int(v, 0, "json_get_bool false = 0");

    /* write result */
    n = jsonrpc_write_result(resp, sizeof(resp), "1", 0, "{\"ok\":true}");
    check_int(n > 0, 1, "write_result returns > 0");

    /* write error */
    n = jsonrpc_write_error(resp, sizeof(resp), "1", 0,
                            JSONRPC_METHOD_NOT_FOUND, "not found");
    check_int(n > 0, 1, "write_error returns > 0");

    /* json_escape */
    n = json_escape("hello \"world\"\nnewline", out, sizeof(out));
    check_int(n > 0, 1, "json_escape non-empty");
    check_str(out, "hello \\\"world\\\"\\nnewline", "json_escape output");

    printf("\n%d test(s) failed.\n", failures);
    return failures > 0 ? 1 : 0;
}
