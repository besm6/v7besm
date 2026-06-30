/*
 * Public interface of the BESM-6 size engine (cmd/size/size.c).
 *
 * Split into a reusable library so it can be linked into both the command-line
 * tool (main.c) and the unit tests.  size_run() parses its own argv exactly
 * like a fresh process would, but returns the exit code instead of calling
 * exit(), so it can be invoked repeatedly in one process.
 */
#ifndef BESM6_SIZE_SIZE_H
#define BESM6_SIZE_SIZE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Print segment sizes of the objects named on argv.  Returns the exit code. */
int size_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* BESM6_SIZE_SIZE_H */
