// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <assert.h> -- diagnostics (C11 §7.2).
//
// Deliberately NOT guarded, and it is the one header in this tree that is not.
// §7.2 says NDEBUG is re-examined at every inclusion, so a source may include
// this file, #define NDEBUG, and include it again to turn the assertions off for
// the rest of the translation unit.  An include guard would silently defeat
// that, which is why `#undef assert' opens the file instead.
//
// v7's macro expanded to a BRACE BLOCK, so `if (p) assert(q); else ...' was a
// syntax error and `assert(q),f()' would not parse at all; §7.2.1.1 requires a
// void EXPRESSION.  It also called fprintf and exit without declaring either,
// which no longer compiles: the front end has no implicit declarations.  The
// handler below is one function instead, __assert_fail in lib/libc/gen/assert.c,
// which writes to fd 2 with write() and calls abort() -- there is no stdio to
// route it through until lib phase 4, and an assertion is exactly the moment not
// to depend on one.
//
// v7's `_assert' is kept as the alias it always was, for the v7 sources that
// still spell it that way.
//
// Nothing is #included: _Noreturn is a keyword, and the handler is declared
// here, so a source that asserts need not drag in stdio or stdlib to do it.

#undef assert
#undef _assert
#undef static_assert

#ifdef NDEBUG

#define assert(ex)  ((void)0)
#define _assert(ex) ((void)0)

#else

_Noreturn void __assert_fail(const char *expr, const char *file, int line);

#define assert(ex)  ((ex) ? (void)0 : __assert_fail(#ex, __FILE__, __LINE__))
#define _assert(ex) assert(ex)

#endif

// §7.2p3: the compile-time form is a macro spelling of the keyword.  The front
// end implements _Static_assert, so this costs nothing.
#define static_assert _Static_assert
