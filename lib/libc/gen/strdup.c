//
// strdup -- a copy of a string, in storage the caller must free.
//
// Neither v7's nor C11's: POSIX has had it since 1988 and C only since C23, and
// v7 wrote it out by hand wherever it was wanted.  This tree did the same --
// cmd/quot/quot.c carries its own three lines of it -- until cmd/cc asked for it
// nine times over in one program (task C9e, cmd/TODO.md), which is the point at
// which a routine belongs in the library rather than in each caller.
//
// Declared in <string.h>, where POSIX puts it, rather than in <stdlib.h> beside
// malloc: it is a string routine that happens to allocate.
//
// NOTHING HERE IS MACHINE-SPECIFIC, which is worth saying because so little else
// in this directory can say it.  A char* is a fat pointer -- a word address and a
// byte offset within the word -- but memcpy() and strlen() already know that, and
// the block malloc() hands back always starts on a word boundary, so the copy is
// word-aligned even when the original was not.
//
#include <stdlib.h>
#include <string.h>

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p  = malloc(n);

    if (p != NULL)
        memcpy(p, s, n);
    return p;
}
