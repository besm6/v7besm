/*
 * Public interface of the BESM-6 nm engine (cmd/nm/nm.c).
 *
 * Split into a reusable library so it can be linked into both the command-line
 * tool (main.c) and the unit tests.  nm_run() parses its own argv exactly like
 * a fresh process would, but returns the exit code instead of calling exit(),
 * so it can be invoked repeatedly in one process.
 */
#ifndef BESM6_NM_NM_H
#define BESM6_NM_NM_H

#ifdef __cplusplus
extern "C" {
#endif

/* List the symbol tables named on argv.  Returns the process exit code. */
int nm_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* BESM6_NM_NM_H */
