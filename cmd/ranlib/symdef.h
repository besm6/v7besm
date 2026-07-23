//
// Public interface of the BESM-6 ranlib engine (cmd/ranlib/ranlib.c).
//
// Split into a reusable library so it can be linked into both the command-line
// tool (main.c) and the unit tests.  ranlib_run() parses its own argv exactly
// like a fresh process would, but returns the exit code instead of calling
// exit(), so it can be invoked repeatedly in one process.
//
// Named symdef.h (after the __.SYMDEF table of contents it builds) rather than
// ranlib.h, to avoid clashing with the cross header besm6/ranlib.h -- the same
// reason cmd/ar uses archive.h instead of ar.h.
//
#ifndef BESM6_RANLIB_SYMDEF_H
#define BESM6_RANLIB_SYMDEF_H

#ifdef __cplusplus
extern "C" {
#endif

// Build the __.SYMDEF index for the archives named on argv.  Returns the
// process exit code.
int ranlib_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // BESM6_RANLIB_SYMDEF_H
