// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Return the ptr in sp at which the character c last appears; NULL if not found.
// See index.c for why this is not a wrapper around strrchr().
//
char *rindex(const char *sp, char c)
{
    const char *r = 0;

    do {
        if (*sp == c)
            r = sp;
    } while (*sp++);
    return (char *)r;
}
