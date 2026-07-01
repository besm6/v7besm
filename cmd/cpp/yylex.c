// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <string.h>

#include "defs.h"

int str_to_int(char *st, int b);

int lex_if_token()
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
        cpp.scan_ptr = skip_blanks(cpp.scan_ptr);
        if (*cpp.tok_ptr == '\n')
            return (stop); // end of #if
        savc          = *cpp.scan_ptr;
        *cpp.scan_ptr = '\0';
        for (p2 = op2 + 8; --p2 >= op2;) // check 2-char ops
            if (0 == strcmp(*p2, cpp.tok_ptr)) {
                val = val2[p2 - op2];
                goto ret;
            }
        s = "+-*/%<>&^|?:!~(),"; // check 1-char ops
        while (*s)
            if (*s++ == *cpp.tok_ptr) {
                val = *--s;
                goto ret;
            }
        if (*cpp.tok_ptr <= '9' && *cpp.tok_ptr >= '0') { // a number
            if (*cpp.tok_ptr == '0')
                cpp.tok_value = (cpp.tok_ptr[1] == 'x' || cpp.tok_ptr[1] == 'X')
                                    ? str_to_int(cpp.tok_ptr + 2, 16)
                                    : str_to_int(cpp.tok_ptr + 1, 8);
            else
                cpp.tok_value = str_to_int(cpp.tok_ptr, 10);
            val = number;
        } else if (isid(*cpp.tok_ptr)) {
            if (0 == strcmp(cpp.tok_ptr, "defined")) {
                ifdef = 1;
                ++cpp.false_level;
                val = DEFINED;
            } else {
                sp = lookup(cpp.tok_ptr, -1);
                if (ifdef != 0) {
                    ifdef = 0;
                    --cpp.false_level;
                }
                cpp.tok_value = (sp->value == 0) ? 0 : 1;
                val           = number;
            }
        } else if (*cpp.tok_ptr == '\'') { // character constant
            val = number;
            if (cpp.tok_ptr[1] == '\\') { // escaped
                if (cpp.scan_ptr[-1] == '\'')
                    cpp.scan_ptr[-1] = '\0';
                s = opc;
                while (*s)
                    if (*s++ != cpp.tok_ptr[2])
                        ++s;
                    else {
                        cpp.tok_value = *s;
                        goto ret;
                    }
                if (cpp.tok_ptr[2] <= '9' && cpp.tok_ptr[2] >= '0')
                    cpp.tok_value = str_to_int(cpp.tok_ptr + 2, 8);
                else
                    cpp.tok_value = cpp.tok_ptr[2];
            } else
                cpp.tok_value = cpp.tok_ptr[1];
        } else if (0 == strcmp("\\\n", cpp.tok_ptr)) {
            *cpp.scan_ptr = savc;
            continue;
        } else {
            *cpp.scan_ptr = savc;
            pperror("Illegal character %c in preprocessor if", *cpp.tok_ptr);
            continue;
        }
    ret:
        *cpp.scan_ptr = savc;
        cpp.out_ptr = cpp.tok_ptr = cpp.scan_ptr;
        return (val);
    }
}

int str_to_int(char *st, int b)
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
