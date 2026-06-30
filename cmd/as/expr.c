//
// Assembler for BESM-6.
// Expression parser.  Operands and the result are full 48-bit values carried
// in as.intval (two 24-bit halves).  Besides the numeric value, every
// expression also has a SEGMENT: which segment, if any, it is relative to.  A
// plain number is "absolute" (SABS); a label is relative to its segment; an
// undefined name is "external" (SEXT).  An expression may contain at most one
// relocatable (non-absolute) term, because the object format can record only a
// single relocation per value - parse_expr enforces that.
//
#include <stdio.h>

#include "as.h"

//
// Parse one primary operand and return its segment, leaving its value in
// as.intval.  An operand is a number, a name, the location counter ".", or a
// parenthesised sub-expression.  "( e )" groups; "{ e }" groups and then masks
// off the exponent field (used when only the mantissa bits are wanted).
//
static int parse_operand(void)
{
    int ty;
    int cval, s;

    switch (next_token(&cval)) {
    default:
        fatal("operand missed");
    case LNUM:
        return SABS; // a literal number: its value is already in as.intval
    case LNAME:
        // A name: its value is the symbol's value, its segment the symbol's
        // segment.  An undefined or common symbol becomes an external
        // reference, remembered in as.extref for the relocation record.
        as.intval.left = as.intval.right = 0;
        ty                               = as.stab[cval].n_type & N_TYPE;
        if (ty == N_UNDF || ty == N_COMM || ty == N_ACOMM) {
            as.extref = cval;
            return SEXT;
        }
        as.intval.right = as.stab[cval].n_value;
        return TYPESEGM(ty);
    case '.':
        // "." is the current location: the offset (in words) into the segment
        // being assembled.  count[] is in half-words, hence the /2.
        as.intval.left  = 0;
        as.intval.right = as.count[as.segm] / 2;
        return as.segm;
    case '(':
        parse_expr(&s);
        if (next_token(&cval) != ')')
            fatal("bad () syntax");
        return s;
    case '{':
        // truncate the exponent
        parse_expr(&s);
        if (next_token(&cval) != '}')
            fatal("bad () syntax");
        as.intval.left &= 07777777L;
        return s;
    }
}

//
// Parse a whole expression and return its value (low 24 bits; the full 48-bit
// value stays in as.intval); the base segment number is stored in *s.
//
//   expression = [operand] {operation operand}...
//   operand    = LNAME | LNUM | "." | "(" expression ")" | "{" expression "}"
//   operation  = "+" | "-" | "&" | "|" | "^" | "~" | "\<" | "\>" | "/" | "*" | "%"
//
// The grammar is a flat left-to-right fold (no operator precedence): each
// operator simply combines the running result with the next operand.  Only '+'
// and '-' may mix a relocatable term with the (absolute) result; every other
// operator requires both sides absolute.  cmdmode is saved and cleared so that
// operand characters are not mistaken for parts of a mnemonic, then restored
// around the look-ahead that may legitimately land on a following instruction.
//
long parse_expr(int *s)
{
    int clex;
    int cval, s2;
    struct word rez;
    int cmd    = as.cmdmode; // a machine instruction may follow the expression
    as.cmdmode = 0;          // operands themselves never absorb operator chars

    // look at the first token
    switch (clex = next_token(&cval)) {
    default:
        // No leading operand (e.g. an empty operand field): value is 0,
        // absolute.  Put the token back for the caller.
        unget_token(clex, cval);
        rez.left = rez.right = 0;
        *s                   = SABS;
        break;
    case LNUM:
    case LNAME:
    case '.':
    case '(':
    case '{':
        unget_token(clex, cval);
        *s  = parse_operand();
        rez = as.intval;
        break;
    }
    // Fold in each "operator operand" pair until a non-operator token appears.
    for (;;) {
        as.cmdmode = cmd; // the look-ahead may land on a mnemonic
        clex       = next_token(&cval);
        as.cmdmode = 0;
        switch (clex) {
            long t;
        case '+':
            // Addition may combine with a relocatable term: if the result so
            // far is absolute it adopts the operand's segment; otherwise the
            // operand must be absolute (two relocatables can't be added).  The
            // arithmetic is a 48-bit add done in 16-bit chunks so the carry can
            // be propagated by hand across the two halves of the word.
            s2 = parse_operand();
            if (*s == SABS)
                *s = s2;
            else if (s2 != SABS)
                fatal("too complex expression");
            t = rez.right >> 16 & 0xffff;
            rez.right &= 0xffff;
            rez.right += as.intval.right & 0xffff;
            if (rez.right & ~0xffff)
                t++;
            rez.right &= 0xffff;
            t += as.intval.right >> 16 & 0xffff;
            rez.right |= t << 16;
            rez.left += as.intval.left;
            if (t & ~0xffff)
                rez.left++;
            break;
        case '-':
            // Subtraction with the same hand-carried 48-bit arithmetic.  The
            // subtrahend must be absolute (you can't subtract a relocatable).
            s2 = parse_operand();
            if (s2 != SABS)
                fatal("too complex expression");
            t = rez.right >> 16 & 0xffff;
            rez.right &= 0xffff;
            rez.right -= as.intval.right & 0xffff;
            if (rez.right & ~0xffff)
                t--;
            rez.right &= 0xffff;
            t -= as.intval.right >> 16 & 0xffff;
            rez.right |= t << 16;
            rez.left -= as.intval.left;
            if (t & ~0xffff)
                rez.left--;
            break;
        // The remaining operators (bitwise, shift, multiply, divide, modulo)
        // all require both sides absolute and act on the full 48-bit value.
        case '&':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left &= as.intval.left;
            rez.right &= as.intval.right;
            break;
        case '|':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left |= as.intval.left;
            rez.right |= as.intval.right;
            break;
        case '^':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left ^= as.intval.left;
            rez.right ^= as.intval.right;
            break;
        case '~': // XOR with the complement (clear the operand's bits)
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left ^= ~as.intval.left;
            rez.right ^= ~as.intval.right;
            break;
        case LLSHIFT: // shift left across the two halves; count is the low 6 bits
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            clex = as.intval.right & 077;
            if (clex < 32) {
                rez.left <<= clex;
                rez.left |= rez.right >> (32 - clex);
                rez.right <<= clex;
            } else {
                rez.left  = rez.right << (clex - 32);
                rez.right = 0;
            }
            break;
        case LRSHIFT: // shift right across the two halves
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            clex = as.intval.right & 077;
            if (clex < 32) {
                rez.right >>= clex;
                rez.right |= rez.left << (32 - clex);
                rez.left >>= clex;
            } else {
                rez.right = rez.left >> (clex - 32);
                rez.left  = 0;
            }
            break;
        // Multiply/divide/modulo work on the low half only (the high half is
        // cleared), so they are effectively 31-bit operations.
        case '*':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left = 0;
            rez.right *= as.intval.right;
            break;
        case '/':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left = 0;
            if (as.intval.right)
                rez.right /= as.intval.right;
            else
                fatal("division by zero");
            break;
        case '%':
            s2 = parse_operand();
            if (*s != SABS || s2 != SABS)
                fatal("too complex expression");
            rez.left = 0;
            if (as.intval.right)
                rez.right %= as.intval.right;
            else
                fatal("division (%%) by zero");
            break;
        default:
            // Not an operator: the expression ends here.  Put the token back,
            // restore cmdmode for the caller, publish the value, and return.
            unget_token(clex, cval);
            as.cmdmode = cmd; // restore for the caller
            as.intval  = rez;
            return rez.right;
        }
    }
    // NOTREACHED
}
