// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <string.h>

#include "defs.h"

int tobinary(char *st, int b);

int yylex()
{
    static int ifdef        = 0;
    static char *op2[]      = { "||", "&&", ">>", "<<", ">=", "<=", "!=", "==" };
    static const int val2[] = { OROR, ANDAND, RS, LS, GE, LE, NE, EQ };
    static const char *opc  = "b\bt\tn\nf\fr\r\\\\";
    char savc;
    const char *s;
    int val;
    char **p2;
    const struct symtab *sp;

    for (;;) {
        cpp.newp = skipbl(cpp.newp);
        if (*cpp.inp == '\n')
            return (stop); // end of #if
        savc      = *cpp.newp;
        *cpp.newp = '\0';
        for (p2 = op2 + 8; --p2 >= op2;) // check 2-char ops
            if (0 == strcmp(*p2, cpp.inp)) {
                val = val2[p2 - op2];
                goto ret;
            }
        s = "+-*/%<>&^|?:!~(),"; // check 1-char ops
        while (*s)
            if (*s++ == *cpp.inp) {
                val = *--s;
                goto ret;
            }
        if (*cpp.inp <= '9' && *cpp.inp >= '0') { // a number
            if (*cpp.inp == '0')
                cpp.yylval = (cpp.inp[1] == 'x' || cpp.inp[1] == 'X') ? tobinary(cpp.inp + 2, 16)
                                                                      : tobinary(cpp.inp + 1, 8);
            else
                cpp.yylval = tobinary(cpp.inp, 10);
            val = number;
        } else if (isid(*cpp.inp)) {
            if (0 == strcmp(cpp.inp, "defined")) {
                ifdef = 1;
                ++cpp.flslvl;
                val = DEFINED;
            } else {
                sp = lookup(cpp.inp, -1);
                if (ifdef != 0) {
                    ifdef = 0;
                    --cpp.flslvl;
                }
                cpp.yylval = (sp->value == 0) ? 0 : 1;
                val        = number;
            }
        } else if (*cpp.inp == '\'') { // character constant
            val = number;
            if (cpp.inp[1] == '\\') { // escaped
                if (cpp.newp[-1] == '\'')
                    cpp.newp[-1] = '\0';
                s = opc;
                while (*s)
                    if (*s++ != cpp.inp[2])
                        ++s;
                    else {
                        cpp.yylval = *s;
                        goto ret;
                    }
                if (cpp.inp[2] <= '9' && cpp.inp[2] >= '0')
                    cpp.yylval = tobinary(cpp.inp + 2, 8);
                else
                    cpp.yylval = cpp.inp[2];
            } else
                cpp.yylval = cpp.inp[1];
        } else if (0 == strcmp("\\\n", cpp.inp)) {
            *cpp.newp = savc;
            continue;
        } else {
            *cpp.newp = savc;
            pperror("Illegal character %c in preprocessor if", *cpp.inp);
            continue;
        }
    ret:
        *cpp.newp = savc;
        cpp.outp = cpp.inp = cpp.newp;
        return (val);
    }
}

int tobinary(char *st, int b)
{
    int n, c, t;
    const char *s;
    n = 0;
    s = st;
    while ((c = *s++)) {
        switch (c) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            t = c - '0';
            break;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            t = c - 'a';
            if (b > 10)
                break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
            t = c - 'A';
            if (b > 10)
                break;
        default:
            t = -1;
            if (c == 'l' || c == 'L')
                if (*s == '\0')
                    break;
            pperror("Illegal number %s", st);
        }
        if (t < 0)
            break;
        n = n * b + t;
    }
    return (n);
}
