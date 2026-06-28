//
// Assembler for BESM-6.
// Lexer: number/name scanning and the token reader.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

static int hexdig(int c)
{
    if (c <= '9')
        return c - '0';
    else if (c <= 'F')
        return c - 'A' + 10;
    else
        return c - 'a' + 10;
}

static void getlhex(void)
{
    int c;
    char *cp, *p;

    // read a hexadecimal number 'ZZZ

    c = getchar();
    for (cp = as.name; ISHEX(c); c = getchar())
        *cp++ = hexdig(c);
    ungetc(c, stdin);
    as.intval.left  = 0;
    as.intval.right = 0;
    p            = as.name;
    for (c = 28; c >= 0; c -= 4, ++p) {
        if (p >= cp)
            return;
        as.intval.left |= (long)*p << c;
    }
    for (c = 28; c >= 0; c -= 4, ++p) {
        if (p >= cp)
            return;
        as.intval.right |= (long)*p << c;
    }
}

// read a hexadecimal number 0xZZZ
static void gethnum(void)
{
    int c;
    char *cp;

    c = getchar();
    for (cp = as.name; ISHEX(c); c = getchar())
        *cp++ = hexdig(c);
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

// Read a number:
//      1234 1234d 1234D - decimal
//      01234 1234. 1234o 1234O - octal
//      1234' 1234h 1234H - hexadecimal
static void getnum(int c)
{
    char *cp;
    int leadingzero;

    leadingzero = (c == '0');
    for (cp = as.name; ISHEX(c); c = getchar())
        *cp++ = hexdig(c);
    as.intval.left  = 0;
    as.intval.right = 0;
    if (c == '.' || c == 'o' || c == 'O') {
    octal:
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
    } else if (c == 'h' || c == 'H' || c == '\'') {
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
        return;
    } else if (c != 'd' && c != 'D') {
        ungetc(c, stdin);
        if (leadingzero)
            goto octal;
    }
    for (c = 1;; c *= 10) {
        if (--cp < as.name)
            return;
        as.intval.right += (long)*cp * c;
    }
}

// Read a number .[a:b], where a, b are bit numbers
// or .[a=b]
static void getbitmask(void)
{
    int c, a, b;
    int v, compl;

    a = getexpr(&v) - 1;
    if (v != SABS)
        uerror("illegal expression in bit mask");
    c = getlex(&v);
    if (c != ':' && c != '=')
        uerror("illegal bit mask delimiter");
    compl = c == '=';
    b     = getexpr(&v) - 1;
    if (v != SABS)
        uerror("illegal expression in bit mask");
    c = getlex(&v);
    if (c != ']')
        uerror("illegal bit mask delimiter");
    if (a < 0 || a >= 64 || b < 0 || b >= 64)
        uerror("bit number out of range 1..64");
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
static void getbitnum(int c)
{
    getnum(c);
    c = as.intval.right - 1;
    if (c < 0 || c >= 64)
        uerror("bit number out of range 1..64");
    if (c >= 32) {
        as.intval.left  = 1L << (c - 32);
        as.intval.right = 0;
    } else if (c >= 0) {
        as.intval.right = 1L << c;
        as.intval.left  = 0;
    }
}

// + - * / are part of operator-bearing mnemonics (a+x, j+m, e-n, ...).
static int isopername(int c)
{
    return c == '+' || c == '-' || c == '*' || c == '/';
}

static void getname(int c)
{
    char *cp;

    // When a machine instruction is expected, absorb the operator characters
    // so mnemonics like "a+x" lex as a single name.
    for (cp = as.name; ISLETTER(c) || ISDIGIT(c) || (as.cmdmode && isopername(c)); c = getchar())
        *cp++ = c;
    *cp = 0;
    ungetc(c, stdin);
}

// int getlex(int *val) - read a token, return its type,
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
int getlex(int *pval)
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
        case '\'':
            getlhex();
            return LNUM;
        case '0':
            if ((c = getchar()) == 'x' || c == 'X') {
                gethnum();
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
            getnum(c);
            return LNUM;
        case '@':
        case '$':
            *pval = hexdig(getchar());
            *pval = *pval << 4 | hexdig(getchar());
            return (c == '$') ? LSCMD : LLCMD;
        default:
            if (!ISLETTER(c))
                uerror("bad character: \\%o", c & 0377);
            if (c == '.') {
                c = getchar();
                if (c == '[') {
                    getbitmask();
                    return LNUM;
                } else if (ISOCTAL(c)) {
                    getbitnum(c);
                    return LNUM;
                }
                ungetc(c, stdin);
                c = '.';
            }
            getname(c);
            if (as.name[0] == '.') {
                if (as.name[1] == 0)
                    return '.';
                if ((*pval = lookacmd()) != -1)
                    return LACMD;
            }
            if ((*pval = lookcmd()) != -1)
                return LCMD;
            *pval = lookname();
            return LNAME;
        }
}

void ungetlex(int val, int type)
{
    as.blexflag = 1;
    as.backlex  = val;
    as.blextype = type;
}
