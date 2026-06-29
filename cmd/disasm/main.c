/*
 * Command-line front end for the BESM-6 disassembler.
 *
 * Parses flags, selects the mnemonic dialect, then disassembles each named
 * a.out file (or "a.out" by default).
 */
#include <stdio.h>

#include "disasm.h"

int main(int argc, char **argv)
{
    int yesarg = 0; /* были ли параметры - имена файлов */

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
                    fprintf(stderr, "Usage: dis [-bcCrR] file...\n");
                    return 1;
                }
            }
        } else {
            disassemble(*argv);
            yesarg = 1;
        }
    }
    if (!yesarg)
        disassemble("a.out");
    return 0;
}
