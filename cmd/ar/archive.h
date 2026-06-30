/*
 * Public interface of the BESM-6 archiver engine (cmd/ar/ar.c).
 *
 * Split into a reusable library so it can be linked into both the command-line
 * tool (main.c) and the unit tests.  ar_run() parses its own argv exactly like
 * a fresh process would, but returns the exit code instead of calling exit(),
 * so it can be invoked repeatedly in one process.
 */
#ifndef BESM6_AR_ARCHIVE_H
#define BESM6_AR_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run the archiver with the given argv.  Returns the process exit code. */
int ar_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* BESM6_AR_ARCHIVE_H */
