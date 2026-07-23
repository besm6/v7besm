//
// vfscanf -- the one call the whole scanf family goes through (C11 §7.21.6.9).
//
// v7 had no v-forms: its scanf(), fscanf() and sscanf() each passed `&args' to
// _doscan.  <stdarg.h> is the only way to walk the arguments here, so the v-form is
// the primitive and the variadic ones are va_start wrappers.
//
#include <stdio.h>

int _doscan(FILE *iop, const char *fmt, va_list ap);

int vfscanf(FILE *iop, const char *fmt, va_list ap)
{
    return _doscan(iop, fmt, ap);
}
