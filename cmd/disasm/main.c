//
// Command-line front end for the BESM-6 disassembler.
//
// Parses flags, selects the mnemonic dialect, then disassembles each named
// a.out file.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"

//
// Print the command-line usage summary and exit with an error.
//
static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [-bcCrR] file...\n", progname);
    printf("Options:\n");
    printf("    -b    Use BEMSH (Cyrillic) mnemonics instead of the default MADLEN\n");
    printf("    -c    Also print each instruction word in octal\n");
    printf("    -C    Print instruction words only in octal (implies -c)\n");
    printf("    -r    Print relocation info\n");
    printf("    -R    Print relocation as numbers (implies -r)\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int yesarg = 0; // whether file-name arguments were given

    // Derive the diagnostic prefix from argv[0]'s basename (fallback "disasm").
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    while (--argc) {
        ++argv;
        if (**argv == '-') {
            char *cp;

            for (cp = *argv + 1; *cp; cp++) {
                switch (*cp) {
                case 'R': // print relocation in numbers
                    Rflag++;
                    // fallthrough
                case 'r': // print relocation info
                    rflag++;
                    break;
                case 'C': // print commands only in octal
                    Cflag++;
                    // fallthrough
                case 'c': // print commands in octal
                    cflag++;
                    break;
                case 'b': // use BEMSH (Cyrillic) mnemonics
                    lcmd = lcmd_bemsh;
                    scmd = scmd_bemsh;
                    break;
                default:
                    usage();
                }
            }
        } else {
            disassemble(*argv);
            yesarg = 1;
        }
    }
    if (!yesarg)
        usage();
    return 0;
}
