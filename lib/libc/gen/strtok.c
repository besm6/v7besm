/* From the BESM-6 c-compiler's own libc: libc/besm6/strtok.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strtok — split a string into tokens separated by delim bytes (C11 §7.24.5.8).
 *
 * The first call passes the string in str; subsequent calls pass NULL to
 * continue from where the previous call left off.  Each call returns the next
 * token (NUL-terminated in place) or NULL when no tokens remain.  Parse state
 * is held in a file-scope static pointer, so strtok is not reentrant.
 */
#include <string.h>

char *strtok(char *str, const char *delim)
{
    static char *save; /* start of the next token search (NULL-init) */
    char *p;
    char *tok;

    if (str != 0) {
        p = str;
    } else {
        p = save;
    }
    if (p == 0) {
        return 0;
    }

    /* Skip leading delimiters. */
    while (*p != 0 && strchr(delim, *p) != 0) {
        p++;
    }
    if (*p == 0) {
        save = 0;
        return 0;
    }

    /* p now points at the token; find its end. */
    tok = p;
    while (*p != 0 && strchr(delim, *p) == 0) {
        p++;
    }
    if (*p == 0) {
        save = 0;
    } else {
        *p = 0;
        p++;
        save = p;
    }
    return tok;
}
