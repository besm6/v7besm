//
// Public interface of the BESM-6 strip engine (cmd/strip/strip.c).
//
// Split into a reusable library so it can be linked into both the command-line
// tool (main.c) and the unit tests.  strip_run() parses its own argv exactly
// like a fresh process would, but returns the exit code instead of calling
// exit(), so it can be invoked repeatedly in one process.
//
#ifndef BESM6_STRIP_STRIP_H
#define BESM6_STRIP_STRIP_H

#ifdef __cplusplus
extern "C" {
#endif

// Strip the symbol table and relocation bits from the named objects.
// Returns the exit code.
int strip_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // BESM6_STRIP_STRIP_H
