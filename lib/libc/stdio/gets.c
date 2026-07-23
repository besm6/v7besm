/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * gets() cannot know how long its buffer is and so cannot be made safe.  C11 §7.21
 * dropped it and this tree keeps it because v7 code calls it; fgets() is the one to
 * use.  The trailing newline is consumed and not stored, which is the one way gets
 * differs from fgets.
 */
#include <stdio.h>

char *gets(char *s)
{
    int c;
    char *cs;

    cs = s;
    while ((c = getchar()) != '\n' && c >= 0)
        *cs++ = c;
    if (c < 0 && cs == s)
        return NULL;
    *cs++ = '\0';
    return s;
}
