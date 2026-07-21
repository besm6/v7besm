/*
 * <stdarg.h> — variable arguments (C11 §7.16), BESM-6 target.
 *
 * The compiler has no va_* builtins, but the BESM-6 calling convention makes a
 * portable implementation trivial: every argument — int, pointer, double, fat
 * char* — occupies exactly one 48-bit word, and arguments sit in consecutive
 * words of the caller's parameter block.  A va_list is therefore just a word
 * pointer that steps one word per va_arg, exactly as the printf engine
 * (madlen/doprnt.c) already walks its arguments with `*ap++`.
 *
 * va_arg(ap, T) reads the raw word and reinterprets it as T.  For char* / void*
 * this is correct because the stored word already IS the fat pointer.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef long *va_list; /* one machine word per argument */

/*
 * Point ap just past the last named argument.  Parameters are laid out in
 * ascending words, so the first variadic argument is at &last + 1.
 */
#define va_start(ap, last) ((ap) = (va_list)&(last) + 1)

/* Fetch the next argument as type T and advance one word. */
#define va_arg(ap, T) (*(T *)(void *)((ap)++))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
