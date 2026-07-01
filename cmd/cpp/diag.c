//
// C preprocessor: diagnostics and small string helpers.
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

static void vreport(const char *s, va_list ap)
{
    if (cpp.inc_file[cpp.inc_level][0]) {
        fprintf(stderr, "%s: ", cpp.inc_file[cpp.inc_level]);
    }
    fprintf(stderr, "%d: ", cpp.line_no[cpp.inc_level]);
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
    ++cpp.exit_code;
}

void pperror(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

void parse_error(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

void ppwarn(const char *s, ...)
{
    int fail = cpp.exit_code;
    va_list ap;

    cpp.exit_code = -1;
    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
    cpp.exit_code = fail;
}

char *dir_of(char *s)
{
    char *p = s;
    while (*p++)
        ;
    --p;
    while (p > s && *--p != '/')
        ;
    if (p == s)
        *p++ = '.';
    *p = '\0';
    return (s);
}

STATIC char *save_string(const char *s)
{
    char *old;

    old = cpp.side_ptr;
    while ((*cpp.side_ptr++ = *s++))
        ;
    return (old);
}

char *find_char(char *s, int c)
{
    while (*s)
        if (*s++ == c)
            return (--s);
    return (0);
}
