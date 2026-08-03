/*
 * The case that walks PAST the BESM-6 build's macro-argument nesting bound on purpose.
 *
 * ../../defs.h's MAXARGDEPTH is 1 there and 200 on the host, so the native cpp stops
 * pre-expanding an argument two calls deep and substitutes the raw text instead -- and
 * this fixture asserts that the two builds STILL EMIT THE SAME BYTES, because the raw
 * text is rescanned in the ordinary way afterwards and expands to the same thing.  It is
 * the whole justification for the bound being a warning rather than an error, and if a
 * future change makes the degradation visible, this is the test that says so.
 *
 * The native build additionally writes a warning to stderr here, which run-cpp-test.sh
 * does not compare; cmd_cpp_nesting is the case that pins that message.
 *
 * WHAT IS NOT HERE, and why: a macro nested two deep INSIDE ITS OWN ARGUMENT --
 * ID(ID(ID(4))) -- is the one shape that genuinely diverges.  The raw text substituted at
 * the bound is rescanned with the outer ID still blue-painted (§6.10.3.4), so the inner
 * call is left un-expanded where the host expands it.  That case belongs to
 * cmd_cpp_nesting, which records the difference as the checked-in fact it is; putting it
 * here would only make this test fail.
 */
#define ID(x)      x
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) MAX(lo, MIN(x, hi))

int two   = ID(ID(2));
int three = MAX(1, MIN(2, 3));
int five  = CLAMP(5, 0, 9);
int six   = MAX(MIN(6, 7), MIN(4, 5));
