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

static void onsig(int sig)
{
    (void)sig;
    cleanup_and_exit();
}

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
