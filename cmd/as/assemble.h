//
// Assembler for BESM-6.
// Public entry point: drive the assembler from a parameter struct instead of
// argv, so it can be invoked by the command-line front end (main.c) and by
// unit tests alike.  This header is intentionally C++-safe (no <stdnoreturn.h>)
// so the GoogleTest suite can include it directly.
//
#ifndef BESM6_AS_ASSEMBLE_H
#define BESM6_AS_ASSEMBLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Command line options for the assembler.
struct assembler_args {
    char *infile;  // input file, NULL = stdin
    char *outfile; // output file, default "a.out"
    int debug;     // -d  debug mode
    int xflags;    // -x  discard local symbols
    int Xflag;     // -X  discard locals starting with '.'
    int uflag;     // -u  treat undefined names as error
    int aflag;     // -a  don't align on word boundary
};

// Run the assembler with the given options.  Returns 0 on success.
int assemble(const struct assembler_args *args);

#ifdef __cplusplus
}
#endif

#endif // BESM6_AS_ASSEMBLE_H
