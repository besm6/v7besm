// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <ctype.h> -- character handling (C11 §7.4).
//
// The twelve classification functions stay v7's table lookup, `(_ctype_ + 1)[c]'
// over lib/libc/gen/ctype_.c, whose table is unchanged from v7 because this
// terminal is ASCII and not KOI7 (kernel/dev/sc.c).  Each macro evaluates its
// argument exactly once, which is what §7.1.4 demands of a library function
// implemented as a macro, so they conform as they stand.
//
// toupper/tolower could NOT stay macros.  v7's are unconditional -- `(c) - 'a' +
// 'A'' -- so v7's toupper('1') returns garbage, where §7.4.2 requires the
// argument back unchanged when it is not a character of the other case.  The
// conditional form must test the class and then yield the argument, i.e.
// evaluate it twice, which §7.1.4 forbids of a macro.  So they are real
// functions here (lib/libc/gen/toupper.c, tolower.c), and v7's unconditional
// pair keeps the name v7 itself gave it on other machines: _toupper/_tolower.
//
// isblank went the same way, for want of a bit rather than for the fold: it is
// the space and the tab, and the tab's only class is _S, which also covers
// newline, vertical tab, form feed and carriage return.  A macro would have had
// to test the two characters separately, evaluating its argument twice.
//
// The classification set is declared as functions as well, because §7.4 wants
// them to exist even where a macro is also provided, so that `(isalpha)(c)' and
// `&isalpha' work.  Those are TODO in libc; the macros serve every caller in the
// tree today.  Declarations come FIRST and the macros after, or the macros would
// eat the declarations.
#ifndef _CTYPE_H
#define _CTYPE_H

// ---- implemented as functions (lib/libc/gen/) ----
int toupper(int c);
int tolower(int c);
int isblank(int c);

// ---- declared for future implementation (TODO); the macros below serve ----
int isalnum(int c);
int isalpha(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);

#define _U 01
#define _L 02
#define _N 04
#define _S 010
#define _P 020
#define _C 040
#define _X 0100
#define _B 0200 // the space; v7 left this bit free -- see lib/libc/gen/ctype_.c

extern char _ctype_[]; // lib/libc/gen/ctype_.c

#define isalpha(c)  ((_ctype_ + 1)[c] & (_U | _L))
#define isupper(c)  ((_ctype_ + 1)[c] & _U)
#define islower(c)  ((_ctype_ + 1)[c] & _L)
#define isdigit(c)  ((_ctype_ + 1)[c] & _N)
#define isxdigit(c) ((_ctype_ + 1)[c] & (_N | _X))
#define isspace(c)  ((_ctype_ + 1)[c] & _S)
#define ispunct(c)  ((_ctype_ + 1)[c] & _P)
#define isalnum(c)  ((_ctype_ + 1)[c] & (_U | _L | _N))
#define iscntrl(c)  ((_ctype_ + 1)[c] & _C)

// isprint counts the space and isgraph does not (§7.4.1.8, §7.4.1.6).  v7 had
// only one macro, spelled as isgraph is here, so v7's isprint(' ') was false;
// the added _B bit is what tells the two apart without a second evaluation.
#define isprint(c) ((_ctype_ + 1)[c] & (_P | _U | _L | _N | _B))
#define isgraph(c) ((_ctype_ + 1)[c] & (_P | _U | _L | _N))

// v7 extensions, not C11.  isascii is the guard that makes a table lookup safe
// for a value above 0177, which runs off the end of a 129-entry table.
#define isascii(c) ((unsigned)(c) <= 0177)
#define toascii(c) ((c) & 0177)

// v7's unconditional case fold, under v7's own name for it.  Valid only where
// the argument is already known to be of the other case.
#define _toupper(c) ((c) - 'a' + 'A')
#define _tolower(c) ((c) - 'A' + 'a')

#endif // _CTYPE_H
