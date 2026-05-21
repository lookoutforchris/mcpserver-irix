/*
 * snprintf.c - snprintf for IRIX 5.3 IDO
 *
 * IRIX 5.3 libc predates snprintf (C99). This provides a safe
 * implementation using vsprintf into an intermediate buffer.
 * Only compiled for the irix53-native target.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Largest buffer used in this project (JSON_MSG_MAX = 65536) */
#define SNPRINTF_TMP_MAX 131072

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
    char   tmp[SNPRINTF_TMP_MAX];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsprintf(tmp, fmt, ap);
    va_end(ap);

    if (n < 0) {
        if (size > 0) buf[0] = '\0';
        return -1;
    }
    if (size == 0)
        return n;
    if ((size_t)n >= size) {
        memcpy(buf, tmp, size - 1);
        buf[size - 1] = '\0';
    } else {
        memcpy(buf, tmp, (size_t)n + 1);
    }
    return n;
}
