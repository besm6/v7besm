//
// Assembler for BESM-6.
// Engine: global state and pass sequencing.  The command-line front end lives
// in main.c; fatal() is supplied by whoever links this library (main.c for the
// CLI, the test harness for unit tests).
//
#include "as.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct assembler as = {
    .outfile   = "a.out",
    .tfilename = "/tmp/asXXXXXX",
};

static void open_temp_files(void)
{
    int i;

    int fd = mkstemp(as.tfilename);
    if (fd == -1) {
        fatal("cannot create temporary file %s", as.tfilename);
    } else {
        close(fd);
    }
    for (i = STEXT; i < SBSS; i++) {
        if (!(as.sfile[i] = fopen(as.tfilename, "w+")))
            fatal("cannot open %s", as.tfilename);
        unlink(as.tfilename);
        if (!(as.rfile[i] = fopen(as.tfilename, "w+")))
            fatal("cannot open %s", as.tfilename);
        unlink(as.tfilename);
    }
    as.line = 1;
}

int assemble(const struct assembler_args *args)
{
    int i;
    int saved_in, saved_out;

    // Reset the global state so the engine can be invoked repeatedly in one
    // process (the unit tests assemble many sources in a row; the CLI front end
    // calls this just once).  Everything else is rebuilt by open_temp_files()/init_hash_tables()/
    // the passes; only the two static defaults need restoring afterwards.
    memset(&as, 0, sizeof as);
    strcpy(as.tfilename, "/tmp/asXXXXXX");

    // Copy options into the global state.
    as.infile  = args->infile;
    as.outfile = args->outfile ? args->outfile : (char *)"a.out";
    as.debug   = args->debug;
    as.xflags  = args->xflags;
    as.Xflag   = args->Xflag;
    as.uflag   = args->uflag;
    as.aflag   = args->aflag;

    // Save the original stdin/stdout so they can be restored when done; the
    // passes read from stdin and write to stdout, so we redirect them here but
    // must not leave them redirected for a caller (e.g. a test process).
    saved_in  = dup(fileno(stdin));
    saved_out = dup(fileno(stdout));

    // set up input/output
    if (as.infile && !freopen(as.infile, "r", stdin))
        fatal("cannot open %s", as.infile);
    if (!freopen(as.outfile, "w", stdout))
        fatal("cannot open %s", as.outfile);

    i = getchar();
    ungetc(i == '#' ? ';' : i, stdin);

    open_temp_files();    // open temporary files
    init_hash_tables();   // initialize hash tables
    generate_code();      // first pass
    finalize_symtab();     // intermediate actions
    write_header(); // write the header
    emit_segments();      // second pass
    write_reloc();  // write relocation files
    write_symtab(); // write the symbol table

    // Restore the original stdin/stdout.
    fflush(stdout);
    if (saved_in >= 0) {
        dup2(saved_in, fileno(stdin));
        close(saved_in);
        clearerr(stdin);
    }
    if (saved_out >= 0) {
        dup2(saved_out, fileno(stdout));
        close(saved_out);
        clearerr(stdout);
    }
    return 0;
}
