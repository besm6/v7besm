// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Return the ptr in sp at which the character c appears; NULL if not found.
//
// strchr() by its older name, and not a wrapper around it: v7 code calls index()
// and ANSI code calls strchr(), and each pulls only its own object out of the
// archive.  The two differ in their prototype only -- index() takes the character
// as a char, which on this machine is an unsigned byte in the low bits of a word.
//
// No header declares this one: it is not ANSI, and v7 has no <strings.h>.
//
char *index(const char *sp, char c)
{
    do {
        if (*sp == c)
            return (char *)sp;
    } while (*sp++);
    return 0;
}
