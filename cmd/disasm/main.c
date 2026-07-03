/*
 * Command-line front end for the BESM-6 disassembler.
 *
 * Parses flags, selects the mnemonic dialect, then disassembles each named
 * a.out file.
 */
#include <stdio.h>
#include <stdlib.h>

#include "disasm.h"

/*
 * Print the command-line usage summary and exit with an error.
 */
static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    b6disasm [-bcCrR] file...\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -b    Use BEMSH (Cyrillic) mnemonics instead of the default MADLEN\n");
    fprintf(stderr, "    -c    Also print each instruction word in octal\n");
    fprintf(stderr, "    -C    Print instruction words only in octal (implies -c)\n");
    fprintf(stderr, "    -r    Print relocation info\n");
    fprintf(stderr, "    -R    Print relocation as numbers (implies -r)\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int yesarg = 0; /* whether file-name arguments were given */

    while (--argc) {
        ++argv;
        if (**argv == '-') {
            char *cp;

            for (cp = *argv + 1; *cp; cp++) {
                switch (*cp) {
                case 'R': /* print relocation in numbers */
                    Rflag++;
                    /* fallthrough */
                case 'r': /* print relocation info */
                    rflag++;
                    break;
                case 'C': /* print commands only in octal */
                    Cflag++;
                    /* fallthrough */
                case 'c': /* print commands in octal */
                    cflag++;
                    break;
                case 'b': /* use BEMSH (Cyrillic) mnemonics */
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
