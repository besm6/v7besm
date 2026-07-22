/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The space and the horizontal tab, and nothing else (C11 SS7.4.1.3).
 *
 * The one classification function that is not also a macro in <ctype.h>, for
 * want of a table bit rather than for want of correctness.  The space has _B,
 * added in the free bit v7 left over so isprint and isgraph could be told apart
 * (ctype_.c); the tab's only class is _S, which also covers newline, vertical
 * tab, form feed and carriage return, so a macro would have had to test the two
 * characters separately and evaluate its argument twice -- which SS7.1.4 forbids.
 */
#include <ctype.h>

int isblank(int c)
{
    return c == ' ' || c == '\t';
}
