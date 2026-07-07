//
// Command-line front end for the BESM-6 linker.
//
// Installs the interrupt handlers, then hands off to ld_link(), which does the
// actual work (cmd/ld/ld.c).  Argument parsing lives in the engine's pass1().
//
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "ld.h"

//
// Signal handler: if the user interrupts the link (Ctrl-C / kill), don't leave a
// half-written output behind - run the normal cleanup and exit.
//
static void onsig(int sig)
{
    (void)sig;
    cleanup_and_exit();
}

//
// Program entry point.  With no arguments, print a usage line.  Otherwise arrange
// for interrupts to clean up, then run the linker engine (ld_link) and exit with
// its result code.  All the real work - option parsing and the two passes - is in
// the engine; this file is just the thin command-line wrapper.
//
int main(int argc, char **argv)
{
    if (argc == 1) {
        printf("Usage:\n");
        printf("    %s [-xXsSrndt] [-T addr] [-D num] [-lname] [-u name] [-e name] [-o file] file...\n",
               argv[0]);
        printf("Options:\n");
        printf("    -o file     Set output file name, default a.out\n");
        printf("    -e name     Make name the program entry point\n");
        printf("    -u name     Enter name as an undefined external (force library load)\n");
        printf("    -D num      Reserve a data segment of at least num words\n");
        printf("    -T addr     Set base load address (octal/hex accepted)\n");
        printf("    -lname      Link library libname.a (bare -l means -la)\n");
        printf("    -x          Discard all local symbols\n");
        printf("    -X          Discard local symbols starting with '.'\n");
        printf("    -S          Strip absolute and debug symbols\n");
        printf("    -s          Discard all symbols\n");
        printf("    -r          Retain relocation; produce relinkable output\n");
        printf("    -n          Pure procedure: read-only text, page-aligned data\n");
        printf("    -d          Define common symbols even under -r\n");
        printf("    -t          Trace progress (repeat for more detail)\n");
        exit(4);
    }
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, onsig);
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
        signal(SIGTERM, onsig);

    exit(ld_link(argc, argv));
}
