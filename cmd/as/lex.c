//
// Assembler for BESM-6.
// Lexer: number/name scanning and the token reader.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

static int hex_digit_value(int c)
{
    if (c <= '9')
        return c - '0';
    else if (c <= 'F')
        return c - 'A' + 10;
    else
        return c - 'a' + 10;
}

// read a hexadecimal number 0xZZZ
static void read_hex_number(void)
{
    int c;
    char *cp;

    c = getchar();
    for (cp = as.name; ISHEX(c); c = getchar())
        *cp++ = hex_digit_value(c);
    ungetc(c, stdin);
    as.intval.left  = 0;
    as.intval.right = 0;
    for (c = 0; c < 32; c += 4) {
        if (--cp < as.name)
            return;
        as.intval.right |= (long)*cp << c;
    }
    for (c = 0; c < 32; c += 4) {
        if (--cp < as.name)
            return;
        as.intval.left |= (long)*cp << c;
    }
}

// read a binary number 0bZZZ
static void read_binary_number(void)
{
    int c;
    char *cp;

    c = getchar();
    for (cp = as.name; c == '0' || c == '1'; c = getchar())
        *cp++ = c - '0';
    ungetc(c, stdin);
    as.intval.left  = 0;
    as.intval.right = 0;
    for (c = 0; c < 32; c++) {
        if (--cp < as.name)
            return;
        as.intval.right |= (long)*cp << c;
    }
    for (c = 0; c < 32; c++) {
        if (--cp < as.name)
            return;
        as.intval.left |= (long)*cp << c;
    }
}

// Read a numeric literal.  The base is fixed by a C++-style prefix:
//      1234  - decimal (no prefix)
//      01234 - octal (leading zero)
// (0x.../0X... and 0b.../0B... are handled by read_hex_number()/read_binary_number().)  Each base
// reads only its own digit class, so a stray non-digit ends the number cleanly.
static void read_number(int c)
{
    char *cp;

    as.intval.left  = 0;
    as.intval.right = 0;
    if (c == '0') {
        // Octal: leading-zero literal, e.g. 0123.
        for (cp = as.name; ISOCTAL(c); c = getchar())
            *cp++ = c - '0';
        ungetc(c, stdin);
        for (c = 0; c <= 27; c += 3) {
            if (--cp < as.name)
                return;
            as.intval.right |= (long)*cp << c;
        }
        if (--cp < as.name)
            return;
        as.intval.right |= (long)*cp << 30;
        as.intval.left = (long)*cp >> 2;
        for (c = 1; c <= 31; c += 3) {
            if (--cp < as.name)
                return;
            as.intval.left |= (long)*cp << c;
        }
        return;
    }

    // Decimal: no prefix, e.g. 1234.
    for (cp = as.name; ISDIGIT(c); c = getchar())
        *cp++ = c - '0';
    ungetc(c, stdin);
    for (c = 1;; c *= 10) {
        if (--cp < as.name)
            return;
        as.intval.right += (long)*cp * c;
    }
}

// Read a number .[a:b], where a, b are bit numbers
// or .[a=b]
static void read_bit_mask(void)
{
    int c, a, b;
    int v, compl;

    a = parse_expr(&v) - 1;
    if (v != SABS)
        fatal("illegal expression in bit mask");
    c = next_token(&v);
    if (c != ':' && c != '=')
        fatal("illegal bit mask delimiter");
    compl = c == '=';
    b     = parse_expr(&v) - 1;
    if (v != SABS)
        fatal("illegal expression in bit mask");
    c = next_token(&v);
    if (c != ']')
        fatal("illegal bit mask delimiter");
    if (a < 0 || a >= 64 || b < 0 || b >= 64)
        fatal("bit number out of range 1..64");
    if (a < b)
        c = a, a = b, b = c;
    if (compl && --a < ++b) {
        as.intval.left  = 0xffffffff;
        as.intval.right = 0xffffffff;
        return;
    }
    // a greater than or equal to b
    if (a >= 32) {
        if (b >= 32) {
            as.intval.left  = (unsigned long)~0L >> (63 - a + b - 32) << (b - 32);
            as.intval.right = 0;
        } else {
            as.intval.left  = (unsigned long)~0L >> (63 - a);
            as.intval.right = (unsigned long)~0L << b;
        }
    } else {
        as.intval.left  = 0;
        as.intval.right = (unsigned long)~0L >> (31 - a + b) << b;
    }
    as.intval.left &= 0xffffffff;
    as.intval.right &= 0xffffffff;
    if (compl) {
        as.intval.left ^= 0xffffffff;
        as.intval.right ^= 0xffffffff;
    }
}

// Read a number .N, where N is a bit number
static void read_bit_number(int c)
{
    read_number(c);
    c = as.intval.right - 1;
    if (c < 0 || c >= 64)
        fatal("bit number out of range 1..64");
    if (c >= 32) {
        as.intval.left  = 1L << (c - 32);
        as.intval.right = 0;
    } else if (c >= 0) {
        as.intval.right = 1L << c;
        as.intval.left  = 0;
    }
}

// + - * / are part of operator-bearing mnemonics (a+x, j+m, e-n, ...).
static int is_operator_char(int c)
{
    return c == '+' || c == '-' || c == '*' || c == '/';
}

static void read_name(int c)
{
    char *cp;

    // When a machine instruction is expected, absorb the operator characters
    // so mnemonics like "a+x" lex as a single name.
    for (cp = as.name; ISLETTER(c) || ISDIGIT(c) || (as.cmdmode && is_operator_char(c)); c = getchar())
        *cp++ = c;
    *cp = 0;
    ungetc(c, stdin);
}

// int next_token(int *val) - read a token, return its type,
// store its value in *val.
// Returned token types:
//      LEOL    - end of line. Value is the number of the line that began.
//      LEOF    - end of file.
//      LNUM    - integer. Value is in intval, *val undefined.
//      LCMD    - machine instruction. Value is its index in table.
//      LNAME   - identifier. Value is its index in stab.
//      LACMD   - assembler directive. Value is its type.
//      LLCMD   - long-address instruction. Value is the code.
//      LSCMD   - short-address instruction. Value is the code.
int next_token(int *pval)
{
    int c;

    if (as.blexflag) {
        as.blexflag = 0;
        *pval    = as.blextype;
        return as.backlex;
    }
    for (;;)
        switch (c = getchar()) {
        case ';':
        skiptoeol:
            while ((c = getchar()) != '\n')
                if (c == EOF)
                    return LEOF;
        case '\n':
            c = getchar();
            if (c == '#')
                goto skiptoeol;
            ungetc(c, stdin);
            *pval = ++as.line;
            return LEOL;
        case ' ':
        case '\t':
            continue;
        case EOF:
            return LEOF;
        case '\\':
            c = getchar();
            if (c == '<')
                return LLSHIFT;
            if (c == '>')
                return LRSHIFT;
            ungetc(c, stdin);
            return '\\';
        case '+':
            if ((c = getchar()) == '+')
                return LINCR;
            ungetc(c, stdin);
            return '+';
        case '-':
            if ((c = getchar()) == '-')
                return LINCR;
            ungetc(c, stdin);
            return '-';
        case '^':
        case '&':
        case '|':
        case '~':
        case '#':
        case '*':
        case '/':
        case '%':
        case '"':
        case ',':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case '<':
        case '>':
        case '=':
        case ':':
            return c;
        case '0':
            c = getchar();
            if (c == 'x' || c == 'X') {
                read_hex_number();
                return LNUM;
            }
            if (c == 'b' || c == 'B') {
                read_binary_number();
                return LNUM;
            }
            ungetc(c, stdin);
            c = '0';
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            read_number(c);
            return LNUM;
        case '@':
        case '$': {
            // Raw opcode $NN (short) or @NN (long); NN is the two-octal-digit
            // opcode number, for opcodes that have no dedicated mnemonic.
            int d1 = getchar(), d2 = getchar();
            if (!ISOCTAL(d1) || !ISOCTAL(d2))
                fatal("bad octal opcode after '%c'", c);
            *pval = (d1 - '0') * 8 + (d2 - '0');
            return (c == '$') ? LSCMD : LLCMD;
        }
        default:
            if (!ISLETTER(c))
                fatal("bad character: \\%o", c & 0377);
            if (c == '.') {
                c = getchar();
                if (c == '[') {
                    read_bit_mask();
                    return LNUM;
                } else if (ISOCTAL(c)) {
                    read_bit_number(c);
                    return LNUM;
                }
                ungetc(c, stdin);
                c = '.';
            }
            read_name(c);
            if (as.name[0] == '.') {
                if (as.name[1] == 0)
                    return '.';
                if ((*pval = lookup_directive()) != -1)
                    return LACMD;
            }
            if ((*pval = lookup_instruction()) != -1)
                return LCMD;
            *pval = lookup_name();
            return LNAME;
        }
}

void unget_token(int val, int type)
{
    as.blexflag = 1;
    as.backlex  = val;
    as.blextype = type;
}
