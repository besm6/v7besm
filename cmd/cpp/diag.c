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

//
// Print one diagnostic line "file: line: message" to stderr, using printf-style
// formatting (the message and its arguments arrive as a va_list).  Every error
// bumps cpp.exit_code so the program can exit non-zero; the callers below wrap
// this with the usual (const char *fmt, ...) interface.
//
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

//
// Report a preprocessor error (printf-style).  Counts toward the exit status.
//
void pperror(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

//
// Report an error found while evaluating a #if expression.  Same as pperror();
// kept separate so the parser has its own named reporting hook.
//
void parse_error(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

//
// Report a warning: print it like an error but leave the exit status alone.
// We save exit_code, force it to -1 so vreport's "++" lands back on 0, print,
// then restore the real count.
//
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

//
// Turn a file path into just its directory, in place: chop everything after the
// last '/'.  A path with no '/' becomes "." (the current directory).  Returns s.
// Used to remember which directory an #include file came from.
//
char *dir_of(char *s)
{
    char *p = s;
    while (*p++) // walk to the terminating '\0'
        ;
    --p;
    while (p > s && *--p != '/') // walk back to the last '/'
        ;
    if (p == s)
        *p++ = '.'; // no '/' at all: the directory is "."
    *p = '\0';
    return (s);
}

//
// Copy a null-terminated string into the side buffer (cpp.side_ptr grows as a
// simple bump allocator) and return a pointer to the stored copy.  Used to keep
// file names, macro names and definition text alive after the scan buffer that
// held them is reused.
//
STATIC char *save_string(const char *s)
{
    char *old;

    old = cpp.side_ptr;
    while ((*cpp.side_ptr++ = *s++)) // copy including the '\0'
        ;
    return (old);
}

//
// Find character c in string s, like strchr(): return a pointer to the first
// occurrence, or null (0) if c is not present.
//
char *find_char(char *s, int c)
{
    while (*s)
        if (*s++ == c)
            return (--s);
    return (0);
}
