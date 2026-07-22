// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <varargs.h> -- the pre-ANSI variadic interface, in BESM-6 terms.
//
// There is nothing for this header to do that <stdarg.h> does not already do.
// Every argument -- int, pointer, double, fat char * -- is exactly one 48-bit
// word, and arguments sit in consecutive words of the caller's parameter block,
// so a va_list is a word pointer that steps one word per va_arg.  That is what
// the compiler's <stdarg.h> already implements; defining a second va_list here
// would only invite the two to disagree.
//
// The K&R form this header exists for -- f(va_alist) va_dcl { ... } -- cannot be
// supported at all: b6cc parses ANSI definitions only, and an old-style parameter
// list does not parse.  va_dcl and va_alist are therefore deliberately NOT
// defined, so such code fails to compile instead of miscompiling.  Convert it to
// (fmt, ...) with va_start(ap, fmt).
//
// <stdarg.h> comes from the compiler's own header directory: cmd/cc appends
// share/besm6/include AFTER the user's -I, which is the same rule that makes
// these v7 headers win over the compiler's (see lib/rules.mk).
#ifndef _VARARGS_H
#define _VARARGS_H

#include <stdarg.h>

#endif // _VARARGS_H
