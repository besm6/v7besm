/*
 * The fixture the host b6cpp and the native /usr/bin/cpp must preprocess into the same
 * bytes.  It exercises what run-cpp-test.sh's header allows: object- and function-like
 * macros, #if/#elif/#else, # and ##, __LINE__/__FILE__, and a local "quoted" include.
 * Deliberately absent: <angle> includes (they would reach the build machine's
 * /usr/include under b6sim) and __DATE__/__TIME__ (they differ between the two runs).
 *
 * ONE LEVEL OF MACRO-ARGUMENT NESTING, no more.  ../../defs.h's MAXARGDEPTH is 1 on the
 * BESM-6 and 200 on the host, so a deeper call is the one construct on which the two
 * builds could legitimately produce different text.  agree_deep.c is the case that walks
 * up to that line on purpose; this one stays inside it.
 */
#include "local.h"

#define ANSWER      42
#define EMPTY
#define STR(s)      #s
#define XSTR(s)     STR(s)
#define CAT(a, b)   a##b
#define XCAT(a, b)  CAT(a, b)
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define VARIADIC(fmt, ...) report(fmt, __VA_ARGS__)
#define NOARGS()    "none"

int object_like    = ANSWER;
int from_header    = LOCAL_ANSWER;
const char *raw    = STR(ANSWER);   /* "ANSWER" -- # takes the raw actual */
const char *cooked = XSTR(ANSWER);  /* "42"     -- prescanned through XSTR */
int pasted         = CAT(ANS, WER); /* ## takes the raw actuals too */
int xpasted        = XCAT(ANS, WER);
int nested         = MAX(ANSWER, LOCAL_ANSWER); /* macros as arguments, depth 1 */
const char *none   = NOARGS();
int spaced         = ANSWER EMPTY + 1;

#if ANSWER > 40 && defined(LOCAL_ANSWER)
int taken = 1;
#elif ANSWER > 20
int taken = 2;
#else
int taken = 3;
#endif

#ifndef NOT_DEFINED
int guarded = 1;
#endif

#undef ANSWER
#ifdef ANSWER
int undef_failed = 1;
#endif

void where(void)
{
    report("%s:%d\n", __FILE__, __LINE__);
    VARIADIC("%d %d\n", 1, 2);
}
