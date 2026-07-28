/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// basename -- strip a directory prefix, and optionally a suffix, from a path.
//
//      basename string [ suffix ]
//
// One of task C2b's five (../README.md), and the only one of them that walks into the
// hazard §2 of that file is about.
//
// THE TWO char * COMPARISONS.  v7's suffix strip is one line:
//
//      while (p1 > p2 && p3 > argv[2])
//              if (*--p3 != *--p1)
//                      goto output;
//
// and BOTH of those relationals are between two char * -- p1 and p2 into argv[1], p3 and
// argv[2] into the suffix.  Neither orders correctly on this machine: a char * carries the
// byte offset in bits 47-45 and the word address in bits 15-1, the offset DECREMENTS as the
// pointer advances, and `>' compiles to an integer comparison of the whole word, so the
// offset field dominates.  There is no relational helper to fix it with.  (Subtraction is
// fine -- b$pdiff decodes both operands -- but ordering is not.)  ../TODO.md credited this
// file with one comparison; it has two, in one expression.
//
// Rewritten as a pair of int indices counting DOWN from each string's end, which is what
// the loop meant in the first place.  date.c's gtime() and ls's makename() are the
// precedents.  Nothing else here compares pointers: the forward scan for the last `/' is
// dereference-and-increment, which is exactly what a fat pointer does correctly.
//
// WHAT THE STRIP DOES WHEN IT RUNS OUT is v7's and is kept: the loop stops as soon as
// EITHER string is exhausted, and truncates unless it stopped on a mismatch.  So
// `basename c .c' prints an empty line -- the suffix is longer than what is left of the
// name, the comparison never disagrees, and the cut lands at the start.  That is the
// upstream behaviour and not something this port introduced; basename.1 now says so.
//
// puts() TAKES ONE ARGUMENT.  v7 wrote `puts(p2, stdout)'; this libc's is C11's, and it
// appends the newline exactly as v7's did, so `puts(p2)' is the whole of the change.
// fputs() would have been wrong -- it does not add the newline.
//
// NOT SETUID: it opens nothing and calls nothing gated.
//
#include <stdio.h>

int main(int argc, char *argv[])
{
    char *base; // the last path component of argv[1]
    int i, j;

    if (argc < 2) {
        putchar('\n');
        return 1;
    }

    // Everything after the last '/' -- and the whole string if there is none.
    base = argv[1];
    for (i = 0; argv[1][i] != '\0'; i++) {
        if (argv[1][i] == '/')
            base = &argv[1][i + 1];
    }

    if (argc > 2) {
        for (i = 0; base[i] != '\0'; i++)
            ;
        for (j = 0; argv[2][j] != '\0'; j++)
            ;
        while (i > 0 && j > 0) {
            if (argv[2][--j] != base[--i])
                goto output;
        }
        base[i] = '\0';
    }

output:
    puts(base);
    return 0;
}
