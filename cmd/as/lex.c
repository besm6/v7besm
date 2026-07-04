//
// Assembler for BESM-6.
// Lexer: it turns the raw input characters into TOKENS - numbers, names,
// instruction mnemonics, directives and operators.  The rest of the assembler
// only ever calls next_token() / unget_token() and never reads characters
// itself.  Numbers are decoded straight into the two-half-word form
// (as.intval) here, because the value can be a full 48-bit word.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

//
// Convert one hexadecimal digit character to its value 0..15.  The caller has
// already checked (via ISHEX) that c really is a hex digit, so only the three
// ranges 0-9 / A-F / a-f need separating.
//
static int hex_digit_value(int c)
{
    if (c <= '9')
        return c - '0';
    else if (c <= 'F')
        return c - 'A' + 10;
    else
        return c - 'a' + 10;
}

//
// Read a hexadecimal literal of the form 0xZZZ (the "0x" is already consumed).
// The digit values are stashed left-to-right in as.name, then assembled into
// the 48-bit value back-to-front: the least significant digit goes to bit 0 of
// the low half, four bits per hex digit, spilling into the high half once the
// low half's 24 bits are full.  The loops stop early when the digit pointer
// runs back past the start, i.e. when all digits are placed.
//
static void read_hex_number(void)
{
    int c;
    char *cp;

    c = getchar();
    for (cp = as.name; ISHEX(c); c = getchar())
        *cp++ = hex_digit_value(c);
    ungetc(c, stdin);
    as.intval = 0;
    // Fill the low half (bits 1..24, i.e. 0-based 0..23), 4 bits per digit,
    // from the last digit up.
    for (c = 0; c < 24; c += 4) {
        if (--cp < as.name)
            return;
        as.intval |= (int64_t)*cp << c;
    }
    // Remaining digits spill into the high half (bits 25..48).
    for (c = 0; c < 24; c += 4) {
        if (--cp < as.name)
            return;
        as.intval |= (int64_t)*cp << (c + 24);
    }
}

//
// Read a binary literal 0bZZZ (the "0b" is already consumed).  Same idea as
// read_hex_number(), but one bit per digit instead of four.
//
static void read_binary_number(void)
{
    int c;
    char *cp;

    c = getchar();
    for (cp = as.name; c == '0' || c == '1'; c = getchar())
        *cp++ = c - '0';
    ungetc(c, stdin);
    as.intval = 0;
    for (c = 0; c < 24; c++) {
        if (--cp < as.name)
            return;
        as.intval |= (int64_t)*cp << c;
    }
    for (c = 0; c < 24; c++) {
        if (--cp < as.name)
            return;
        as.intval |= (int64_t)*cp << (c + 24);
    }
}

//
// Read a numeric literal.  The base is fixed by a C-style prefix:
//      1234  - decimal (no prefix)
//      01234 - octal (leading zero)
// (0x.../0X... and 0b.../0B... are handled by read_hex_number()/
// read_binary_number().)  Each base reads only its own digit class, so a stray
// non-digit ends the number cleanly.  As above, the value lands in as.intval.
//
static void read_number(int c)
{
    as.intval = 0;
    if (c == '0') {
        char *cp;
        // Octal: leading-zero literal, e.g. 0123.  Three bits per digit.
        for (cp = as.name; ISOCTAL(c); c = getchar())
            *cp++ = c - '0';
        ungetc(c, stdin);
        // Pack eight digits (bits 1..24, i.e. 0-based 0..23) into the low half.
        for (c = 0; c <= 21; c += 3) {
            if (--cp < as.name)
                return;
            as.intval |= (int64_t)*cp << c;
        }
        // Further digits continue in the high half.  24 is a multiple of 3, so
        // octal digits align on the half-word boundary - no straddling digit.
        for (c = 0; c <= 21; c += 3) {
            if (--cp < as.name)
                return;
            as.intval |= (int64_t)*cp << (c + 24);
        }
        return;
    }

    // Decimal: no prefix, e.g. 1234.  Accumulate into a wide host value, then
    // mask to 48 bits so a large decimal does not leak past the word.
    {
        unsigned long long v = 0;
        for (; ISDIGIT(c); c = getchar())
            v = v * 10 + (c - '0');
        ungetc(c, stdin);
        as.intval = (int64_t)(v & WORD_MASK);
    }
}

//
// Read a bit-mask literal of the form .[a:b] or .[a=b], where a and b are bit
// numbers (1-based, 1..64).  It yields a 48-bit value with the bits from b up
// to a set.  The ':' form sets that range; the '=' form sets the COMPLEMENT
// (everything outside the range) - "compl" remembers which.  Bit numbers are
// converted to 0-based here (the "- 1").  The shifts below build the mask in
// the two 24-bit halves (low half = value bits 0..23, high half = value bits
// 24..47, OR'd into as.intval shifted up by 24).
//
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
    if (a < b) // make a the high end, b the low end
        c = a, a = b, b = c;
    // For the '=' (complement) form an empty range means "set everything".
    if (compl && --a < ++b) {
        as.intval = WORD_MASK;
        return;
    }
    // a >= b.  Set bits b..a, split across the two 24-bit halves: bit range
    // b..min(a,23) in the low half, max(b,24)..min(a,47) in the high half.
    // A contiguous run of bits lo..hi is (~0 >> (63 - (hi - lo))) << lo.
    {
        unsigned long m = ~0UL;
        as.intval       = 0;
        if (b <= 23) {
            int hi = a < 23 ? a : 23;
            as.intval |= (int64_t)((m >> (63 - (hi - b)) << b) & 077777777UL);
        }
        if (a >= 24) {
            int lo = (b > 24 ? b : 24) - 24;
            int hi = (a < 47 ? a : 47) - 24;
            as.intval |= (int64_t)((m >> (63 - (hi - lo)) << lo) & 077777777UL) << 24;
        }
    }
    if (compl)
        as.intval ^= WORD_MASK;
}

//
// Read a single-bit literal .N, where N is a 1-based bit number (1..64).  It
// yields a value with just that one bit set.  Reuses read_number() to parse N.
//
static void read_bit_number(int c)
{
    read_number(c);
    c = (int)as.intval - 1;
    if (c < 0 || c >= 64)
        fatal("bit number out of range 1..64");
    // Split at bit 24 so each shift amount stays within one half-word.
    if (c >= 24)
        as.intval = (int64_t)(1L << (c - 24)) << 24;
    else if (c >= 0)
        as.intval = 1L << c;
}

//
// True for the four characters that appear inside operator-bearing mnemonics
// (a+x, j+m, e-n, a/x, ...).  Used only when the lexer is in cmdmode.
//
static int is_operator_char(int c)
{
    return c == '+' || c == '-' || c == '*' || c == '/';
}

//
// Read an identifier into as.name (NUL-terminated).  Normally a name is
// letters and digits, but when a machine instruction is expected (cmdmode)
// the '+ - * /' characters are also pulled in, so "a+x" scans as one name.
//
static void read_name(int c)
{
    char *cp;

    // When a machine instruction is expected, absorb the operator characters
    // so mnemonics like "a+x" lex as a single name.
    for (cp = as.name; ISLETTER(c) || ISDIGIT(c) || (as.cmdmode && is_operator_char(c));
         c  = getchar())
        *cp++ = c;
    *cp = 0;
    ungetc(c, stdin);
}

//
// Read one token from the input, return its kind, and store its value in
// *pval.  This is the heart of the lexer; everything else in the assembler
// reads the source only through here.  The kinds and what *pval carries:
//      LEOL    - end of line. *pval is the number of the line that just began.
//      LEOF    - end of file.
//      LNUM    - integer. The value is in as.intval; *pval is unused.
//      LCMD    - machine instruction. *pval is its index in table[].
//      LNAME   - identifier. *pval is its index in the symbol table.
//      LACMD   - assembler directive. *pval is its directive code.
//      LLCMD   - raw long-address opcode. *pval is the opcode number.
//      LSCMD   - raw short-address opcode. *pval is the opcode number.
// Any other single character (operators, brackets, ...) is returned as its own
// ASCII code.  One pushed-back token (see unget_token) is returned first.
//
int next_token(int *pval)
{
    int c;

    // If a token was pushed back, hand it straight back.
    if (as.blexflag) {
        as.blexflag = 0;
        *pval       = as.blextype;
        return as.backlex;
    }
    for (;;)
        switch (c = getchar()) {
        skiptoeol:
            // Comment: skip the rest of the line, then fall into the newline
            // handling below.  Reached only via goto (from the "//" and the
            // line-start '#' handlers).
            while ((c = getchar()) != '\n')
                if (c == EOF)
                    return LEOF;
        case '\n':
            // A '#' at the very start of the next line also begins a comment
            // (cc-style line markers), so loop back and skip it.
            c = getchar();
            if (c == '#')
                goto skiptoeol;
            ungetc(c, stdin);
            *pval = ++as.line;
            return LEOL;
        case ' ':
        case '\t':
            continue; // whitespace separates tokens but is not one
        case EOF:
            return LEOF;
        case '\\':
            // "\<" and "\>" are the shift operators; a bare backslash is itself.
            c = getchar();
            if (c == '<')
                return LLSHIFT;
            if (c == '>')
                return LRSHIFT;
            ungetc(c, stdin);
            return '\\';
        case '+':
            // "++" is one token, otherwise a plain '+'.
            if ((c = getchar()) == '+')
                return LINCR;
            ungetc(c, stdin);
            return '+';
        case '-':
            if ((c = getchar()) == '-')
                return LINCR;
            ungetc(c, stdin);
            return '-';
        case '/':
            // "//" starts a comment to end of line (C-style); a single '/' is
            // the division operator.
            if ((c = getchar()) == '/')
                goto skiptoeol;
            ungetc(c, stdin);
            return '/';
        case '^':
        case '&':
        case '|':
        case '~':
        case '#':
        case '*':
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
            return c; // single-character tokens: return the character itself
        case '0':
            // A leading zero may introduce a 0x hex or 0b binary literal;
            // otherwise it is an octal literal, so push the next char back and
            // fall through to the digit handling with c == '0'.
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
            // Anything else must begin a name (letters, '.', '_', ...).
            if (!ISLETTER(c))
                fatal("bad character: \\%o", c & 0377);
            if (c == '.') {
                // A '.' may start a bit literal: ".[a:b]" mask or ".N" bit.
                c = getchar();
                if (c == '[') {
                    read_bit_mask();
                    return LNUM;
                } else if (ISOCTAL(c)) {
                    read_bit_number(c);
                    return LNUM;
                }
                // Plain '.' (or ".name" directive): undo the look-ahead.
                ungetc(c, stdin);
                c = '.';
            }
            read_name(c);
            // A name starting with '.' is either the location-counter token "."
            // or a directive like ".text".
            if (as.name[0] == '.') {
                if (as.name[1] == 0)
                    return '.';
                if ((*pval = lookup_directive()) != -1)
                    return LACMD;
            }
            // Otherwise it is a machine-instruction mnemonic if it matches the
            // table, else an ordinary symbol name.
            if ((*pval = lookup_instruction()) != -1)
                return LCMD;
            *pval = lookup_name();
            return LNAME;
        }
}

//
// Push one token back so the next next_token() returns it again.  Only a
// single token of look-ahead is supported, which is all the parser needs.
//
void unget_token(int val, int type)
{
    as.blexflag = 1;
    as.backlex  = val;
    as.blextype = type;
}
