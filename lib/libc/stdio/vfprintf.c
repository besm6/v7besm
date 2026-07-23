//
// vfprintf -- the one call the whole printf family goes through (C11 §7.21.6.8).
//
// v7 had no v-forms; its printf(), fprintf() and sprintf() each took `&args' and
// handed that to _doprnt.  Here <stdarg.h> is the only way to walk the arguments,
// so the v-form is the primitive and the variadic ones are va_start wrappers.
//
// The count comes back from the engine; ferror() is what turns a stream that went
// bad into the negative value §7.21.6.1 asks for.
//
#include <stdio.h>

int _doprnt(const char *fmt, va_list ap, FILE *iop);

int vfprintf(FILE *iop, const char *fmt, va_list ap)
{
    int n;

    n = _doprnt(fmt, ap, iop);
    return ferror(iop) ? EOF : n;
}
