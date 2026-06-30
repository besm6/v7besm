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
        printf("Usage: %s [-xXsSrndt] [-lname] [-D num] [-u name] [-e name] [-o file] file...\n",
               argv[0]);
        exit(4);
    }
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, onsig);
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
        signal(SIGTERM, onsig);

    exit(ld_link(argc, argv));
}
