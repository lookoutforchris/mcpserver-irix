/*
 * json.c - minimal JSON-RPC 2.0 message handling
 *
 * See json.h for the full API contract.
 */

#include "json.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * skip_ws - return pointer past leading whitespace in s.
 */
static const char *
skip_ws(const char *s)
{
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++;
    return s;
}

/*
 * find_field - locate `"field":` in a JSON object string.
 * Returns pointer to the start of the value, or NULL if not found.
 *
 * Limitation: only handles top-level fields; does not recurse.
 * Sufficient for flat JSON-RPC message envelopes.
 */
static const char *
find_field(const char *json, const char *field)
{
    char   needle[JSON_METHOD_MAX + 4]; /* "field": */
    size_t nlen;
    const char *p;

    if ((int)(strlen(field) + 4) >= JSON_METHOD_MAX + 4)
        return NULL;

    needle[0] = '"';
    strcpy(needle + 1, field);
    nlen = strlen(field);
    needle[1 + nlen] = '"';
    needle[2 + nlen] = '\0';

    p = json;
    while ((p = strstr(p, needle)) != NULL) {
        const char *after = p + strlen(needle);
        after = skip_ws(after);
        if (*after == ':')
            return skip_ws(after + 1);
        p++;
    }
    return NULL;
}

/*
 * read_string - read a JSON string value into out, unescaping \n \t \" \\ etc.
 * val must point at the opening '"'.
 * Returns number of bytes written to out (not including null), -1 on error.
 */
static int
read_string(const char *val, char *out, size_t outsz)
{
    const char *p;
    size_t      n = 0;

    if (*val != '"')
        return -1;
    p = val + 1;

    while (*p && *p != '"') {
        if (n >= outsz - 1)
            return -1; /* truncation would occur */

        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  out[n++] = '"';  break;
            case '\\': out[n++] = '\\'; break;
            case '/':  out[n++] = '/';  break;
            case 'n':  out[n++] = '\n'; break;
            case 'r':  out[n++] = '\r'; break;
            case 't':  out[n++] = '\t'; break;
            case 'b':  out[n++] = '\b'; break;
            case 'f':  out[n++] = '\f'; break;
            default:   out[n++] = *p;   break; /* pass through */
            }
        } else {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int
jsonrpc_parse(const char *line, struct jsonrpc_request *req)
{
    const char *val;
    size_t      linelen;

    if (!line || !req)
        return -1;

    linelen = strlen(line);
    if (linelen == 0 || linelen >= JSON_MSG_MAX)
        return -1;

    memcpy(req->buf, line, linelen + 1);
    req->id[0]     = '\0';
    req->id_is_null = 0;
    req->method[0] = '\0';
    req->params_start = NULL;

    /* extract "method" */
    val = find_field(req->buf, "method");
    if (!val || *val != '"')
        return -1;
    if (read_string(val, req->method, JSON_METHOD_MAX) < 0)
        return -1;

    /* extract "id" (may be string, number, or null) */
    val = find_field(req->buf, "id");
    if (val) {
        val = skip_ws(val);
        if (strncmp(val, "null", 4) == 0) {
            req->id_is_null = 1;
        } else if (*val == '"') {
            read_string(val, req->id, JSON_ID_MAX);
        } else {
            /* numeric id: copy digits */
            int n = 0;
            while (*val && *val != ',' && *val != '}' &&
                   n < JSON_ID_MAX - 1)
                req->id[n++] = *val++;
            req->id[n] = '\0';
        }
    } else {
        req->id_is_null = 1; /* notification */
    }

    /* locate "params" raw value */
    val = find_field(req->buf, "params");
    req->params_start = val ? (char *)val : NULL;

    return 0;
}

int
jsonrpc_write_result(char *buf, size_t bufsz,
                     const char *id, int id_is_null,
                     const char *result_json)
{
    int n;

    if (id_is_null || !id || id[0] == '\0') {
        n = snprintf(buf, bufsz,
                     "{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":%s}\n",
                     result_json ? result_json : "null");
    } else {
        /* determine if id is a bare number */
        const char *p = id;
        int numeric = 1;
        while (*p) {
            if (*p < '0' || *p > '9') { numeric = 0; break; }
            p++;
        }
        if (numeric) {
            n = snprintf(buf, bufsz,
                         "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}\n",
                         id, result_json ? result_json : "null");
        } else {
            n = snprintf(buf, bufsz,
                         "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":%s}\n",
                         id, result_json ? result_json : "null");
        }
    }
    return (n > 0 && (size_t)n < bufsz) ? n : -1;
}

int
jsonrpc_write_error(char *buf, size_t bufsz,
                    const char *id, int id_is_null,
                    int code, const char *message)
{
    char escaped[512];
    int  n;

    json_escape(message ? message : "error", escaped, sizeof(escaped));

    if (id_is_null || !id || id[0] == '\0') {
        n = snprintf(buf, bufsz,
                     "{\"jsonrpc\":\"2.0\",\"id\":null,"
                     "\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                     code, escaped);
    } else {
        const char *p = id;
        int numeric = 1;
        while (*p) {
            if (*p < '0' || *p > '9') { numeric = 0; break; }
            p++;
        }
        if (numeric) {
            n = snprintf(buf, bufsz,
                         "{\"jsonrpc\":\"2.0\",\"id\":%s,"
                         "\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                         id, code, escaped);
        } else {
            n = snprintf(buf, bufsz,
                         "{\"jsonrpc\":\"2.0\",\"id\":\"%s\","
                         "\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                         id, code, escaped);
        }
    }
    return (n > 0 && (size_t)n < bufsz) ? n : -1;
}

int
json_escape(const char *in, char *out, size_t outsz)
{
    size_t n = 0;
    const char *p = in;

    while (*p && n < outsz - 2) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            if (n + 2 >= outsz) break;
            out[n++] = '\\'; out[n++] = '"';
        } else if (c == '\\') {
            if (n + 2 >= outsz) break;
            out[n++] = '\\'; out[n++] = '\\';
        } else if (c == '\n') {
            if (n + 2 >= outsz) break;
            out[n++] = '\\'; out[n++] = 'n';
        } else if (c == '\r') {
            if (n + 2 >= outsz) break;
            out[n++] = '\\'; out[n++] = 'r';
        } else if (c == '\t') {
            if (n + 2 >= outsz) break;
            out[n++] = '\\'; out[n++] = 't';
        } else if (c < 0x20) {
            /* other control characters: skip */
        } else {
            out[n++] = (char)c;
        }
        p++;
    }
    out[n] = '\0';
    return (int)n;
}

int
json_get_string(const char *json, const char *field, char *out, size_t outsz)
{
    const char *val = find_field(json, field);
    if (!val || *val != '"')
        return -1;
    return (read_string(val, out, outsz) >= 0) ? 0 : -1;
}

int
json_get_int(const char *json, const char *field, int *out)
{
    const char *val = find_field(json, field);
    char *endp;
    long  v;

    if (!val)
        return -1;
    val = skip_ws(val);
    if (*val == '-' || (*val >= '0' && *val <= '9')) {
        v = strtol(val, &endp, 10);
        if (endp == val)
            return -1;
        *out = (int)v;
        return 0;
    }
    return -1;
}

int
json_get_bool(const char *json, const char *field, int *out)
{
    const char *val = find_field(json, field);
    if (!val)
        return -1;
    val = skip_ws(val);
    if (strncmp(val, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(val, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

/*
 * scan_value_end - return pointer past the end of one JSON value starting at p.
 * Handles strings, numbers, booleans, null, objects, and arrays.
 */
static const char *
scan_value_end(const char *p)
{
    int   depth;
    char  open, close;

    p = skip_ws(p);

    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            if (*p) p++;
        }
        return *p ? p + 1 : NULL;
    }

    if (*p == '{' || *p == '[') {
        open  = *p;
        close = (*p == '{') ? '}' : ']';
        depth = 0;
        while (*p) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\') p++;
                    if (*p) p++;
                }
                if (*p) p++;
                continue;
            }
            if (*p == open)  depth++;
            if (*p == close) { depth--; if (depth == 0) return p + 1; }
            p++;
        }
        return NULL;
    }

    /* number, boolean, null: scan until delimiter */
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        p++;
    return p;
}

int
json_get_object(const char *json, const char *field, char *out, size_t outsz)
{
    const char *val;
    const char *end;
    size_t      n;

    val = find_field(json, field);
    if (!val) return -1;
    val = skip_ws(val);

    if (*val != '{' && *val != '[') return -1;

    end = scan_value_end(val);
    if (!end) return -1;

    n = (size_t)(end - val);
    if (n >= outsz) return -1;

    memcpy(out, val, n);
    out[n] = '\0';
    return 0;
}

int
json_get_string_array(const char *json, const char *field,
                       char *out_buf, int item_maxlen, int maxcount)
{
    const char *val;
    int         count = 0;
    char       *slot;

    val = find_field(json, field);
    if (!val) return -1;
    val = skip_ws(val);
    if (*val != '[') return -1;
    val++;

    while (count < maxcount) {
        val = skip_ws(val);
        if (*val == ']') break;
        if (*val == ',') { val++; continue; }

        if (*val == '"') {
            slot = out_buf + count * item_maxlen;
            if (read_string(val, slot, (size_t)item_maxlen) < 0)
                return -1;
            count++;
            /* advance past this string */
            val++;
            while (*val && *val != '"') {
                if (*val == '\\') val++;
                if (*val) val++;
            }
            if (*val) val++; /* past closing " */
        } else {
            /* skip non-string element */
            val = scan_value_end(val);
            if (!val) break;
        }
    }

    return count;
}
