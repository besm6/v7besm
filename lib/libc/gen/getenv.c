// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// getenv(name) -- ptr to the value associated with name, if any, else NULL.
//
// `environ' is crt0's (lib/libc/csu/crt0.s): the gate lays argc, the argv[] vector,
// a null, the envp[] vector and a second null at the fixed base 070000, and crt0
// computes &block[argc+2] into this word.  Every slot strides by ONE word -- a
// `char *' is a fat pointer but a `char **' is a plain word address -- and only the
// strings themselves are byte-packed, so all but the first carries a byte offset.
// Nothing here needs to know that: the fat pointer walks its own way.
//
extern char **environ;

//
// s1 is either name, or name=value; s2 is name=value.
// If the names match, return the value of s2, else NULL.
//
static char *nvmatch(const char *s1, char *s2)
{
    while (*s1 == *s2++)
        if (*s1++ == '=')
            return s2;
    if (*s1 == '\0' && *(s2 - 1) == '=')
        return s2;
    return 0;
}

char *getenv(const char *name)
{
    char **p = environ;
    char *v;

    while (*p != 0)
        if ((v = nvmatch(name, *p++)) != 0)
            return v;
    return 0;
}
