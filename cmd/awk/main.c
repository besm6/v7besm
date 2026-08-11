// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "awk.h"
#include "y.tab.h"

#define TOLOWER(c) (isupper(c) ? tolower(c) : c)

int svargc;
char **svargv;
extern FILE *yyin; // lex input file
char *lexprog;     // points to program argument if it exists

// v7's -S, -R and -d are gone, with freeze.c, dprintf and logit(); see README.md.
int main(int argc, char *argv[])
{
    static char *dash[] = { "-" };

    if (argc == 1)
        error(FATAL, "Usage: awk [-f source | 'cmds'] [files]");
    procinit();
    syminit();
    fldinit();
    while (argc > 1) {
        argc--;
        argv++;
        // This nonsense is because gcos argument handling folds -F into -f; accordingly
        // one checks the character after f to see if it is -f file or -Fx.
        if (argv[0][0] == '-' && TOLOWER(argv[0][1]) == 'f' && argv[0][2] == '\0') {
            yyin = fopen(argv[1], "r");
            if (yyin == NULL)
                error(FATAL, "can't open %s", argv[1]);
            argc--;
            argv++;
            break;
        } else if (argv[0][0] == '-' && TOLOWER(argv[0][1]) == 'f') { // set field sep
            if (argv[0][2] == 't')                                    // special case for tab
                **FS = '\t';
            else
                **FS = argv[0][2];
            continue;
        } else if (argv[0][0] != '-') {
            yyin    = NULL;
            lexprog = argv[0];
            break;
        }
    }
    argc--;
    argv++;
    if (argc <= 0) {
        // Read the standard input.  v7 wrote "-" back through argv[0] and stepped argv
        // below its own base to say this.
        svargc = 1;
        svargv = dash;
    } else {
        svargc = argc;
        svargv = argv;
    }
    *FILENAME = *svargv; // initial file name
    yyparse();
    if (errorflag)
        exit(errorflag);
    run();
    exit(errorflag);
}

int yywrap(void)
{
    return 1;
}
