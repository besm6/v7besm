//
// Assembler for BESM-6.
// Expression parser.  Operands and the result are full 48-bit values carried
// in as.intval (one int64_t).  Besides the numeric value, every
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
        as.intval = 0;
        ty        = as.stab[cval].n_type & N_TYPE;
        if (ty == N_UNDF || ty == N_COMM) {
            as.extref = cval;
            return SEXT;
        }
        as.intval = as.stab[cval].n_value;
        return TYPESEGM(ty);
    case '.':
        // "." is the current location: the offset (in words) into the segment
        // being assembled.  count[] is in half-words, hence the /2.
        as.intval = as.count[as.segm] / 2;
        return as.segm;
    case '(':
        parse_expr(&s);
        if (next_token(&cval) != ')')
            fatal("bad () syntax");
        return s;
    case '{':
        // truncate the exponent: clear the 7-bit exponent field (bits 42..48),
        // keeping the sign and mantissa.
        parse_expr(&s);
        if (next_token(&cval) != '}')
            fatal("bad () syntax");
        as.intval &= ~((int64_t)0177 << 41);
        return s;
    }
}

//
// Parse a term: a chain of unary prefixes applied to one operand.  Unary '-'
// and '~' negate/complement the 48-bit value and demand an absolute operand
// (you cannot negate a relocatable); unary '+' is the identity and keeps the
// operand's segment, so "+label" stays relocatable.  The value is left in
// as.intval and the segment returned, as for parse_operand.
//
//   term = {"-" | "~" | "+"} term | operand
//
static int parse_term(void)
{
    int clex, cval, s;

    switch (clex = next_token(&cval)) {
    case '-':
        s = parse_term();
        if (s != SABS)
            fatal("too complex expression");
        as.intval = (0 - as.intval) & WORD_MASK;
        return SABS;
    case '~':
        s = parse_term();
        if (s != SABS)
            fatal("too complex expression");
        as.intval = ~as.intval & WORD_MASK;
        return SABS;
    case '+':
        return parse_term();
    default:
        // Not a prefix: the token belongs to the operand.  A missing operand is
        // an error here - only a whole expression may be empty (see parse_expr).
        unget_token(clex, cval);
        return parse_operand();
    }
}

//
// Return the binding strength of a binary operator: bigger means it binds
// tighter (e.g. '*' beats '+').  Zero means the token is not an operator, which
// is what ends an expression.  The levels mirror C, with the BESM-6 "xor with
// complement" operator '~' sharing a level with its sibling '^'.
//
static int precedence(int token)
{
    switch (token) {
    case '|':
        return 1;
    case '^':
    case '~':
        return 2;
    case '&':
        return 3;
    case LLSHIFT:
    case LRSHIFT:
        return 4;
    case '+':
    case '-':
        return 5;
    case '*':
    case '/':
    case '%':
        return 6;
    default:
        return 0;
    }
}

//
// Apply a binary operator to the running value *lhs (in segment lseg) and the
// already-reduced right operand rhs (in segment rseg).  The result lands in
// *lhs and its segment is returned.  Only '+' and '-' may involve a relocatable
// term; every other operator requires both sides absolute.
//
// Note that rhs is passed by value rather than read from as.intval: by the time
// we get here the look-ahead inside parse_binary may have lexed a number and
// overwritten as.intval.
//
static int apply_op(int op, int64_t *lhs, int lseg, int64_t rhs, int rseg)
{
    switch (op) {
    case '+':
        // Addition may combine with a relocatable term: if the result so far is
        // absolute it adopts the operand's segment; otherwise the operand must
        // be absolute (two relocatables can't be added).  Masked to 48 bits.
        if (lseg == SABS)
            lseg = rseg;
        else if (rseg != SABS)
            fatal("too complex expression");
        *lhs = (*lhs + rhs) & WORD_MASK;
        return lseg;
    case '-':
        // Subtraction, masked back to 48 bits.  The subtrahend must be absolute
        // (you can't subtract a relocatable).
        if (rseg != SABS)
            fatal("too complex expression");
        *lhs = (*lhs - rhs) & WORD_MASK;
        return lseg;
    }

    // The remaining operators (bitwise, shift, multiply, divide, modulo) all
    // require both sides absolute and act on the full 48-bit value.
    if (lseg != SABS || rseg != SABS)
        fatal("too complex expression");

    switch (op) {
    case '&':
        *lhs &= rhs;
        break;
    case '|':
        *lhs |= rhs;
        break;
    case '^':
        *lhs ^= rhs;
        break;
    case '~': // XOR with the complement (clear the operand's bits)
        *lhs ^= ~rhs & WORD_MASK;
        break;
    case LLSHIFT: // shift the whole 48-bit value left; count is the low 6 bits
        *lhs = ((uint64_t)*lhs << (rhs & 077)) & WORD_MASK;
        break;
    case LRSHIFT: // shift the whole 48-bit value right
        *lhs = (uint64_t)*lhs >> (rhs & 077);
        break;
    // Multiply/divide/modulo take the low 24 bits of each side; multiply may
    // then produce a full 48-bit product, divide/modulo a 24-bit result.
    case '*':
        *lhs = (*lhs & HALF_MASK) * (rhs & HALF_MASK);
        break;
    case '/':
        if (rhs & HALF_MASK)
            *lhs = (*lhs & HALF_MASK) / (rhs & HALF_MASK);
        else
            fatal("division by zero");
        break;
    case '%':
        if (rhs & HALF_MASK)
            *lhs = (*lhs & HALF_MASK) % (rhs & HALF_MASK);
        else
            fatal("division (%%) by zero");
        break;
    }
    return SABS;
}

//
// Fold "operator term" pairs onto the running value *lhs by precedence
// climbing: an operator is consumed only if it binds at least as tightly as
// minprec, and its right operand absorbs everything that binds strictly tighter
// (hence the recursion at prec+1, which makes every operator left-associative).
// Returns the segment of the result.
//
// cmdmode is restored around each look-ahead, at every level of the recursion:
// the token that ends the expression is discovered at whatever depth the climb
// bottoms out, and a pushed-back token keeps the kind it was lexed with - so a
// following mnemonic ("7 xta data") must be recognized right there.
//
static int parse_binary(int64_t *lhs, int lseg, int minprec, int cmd)
{
    for (;;) {
        int cval, rseg;
        int64_t rhs;

        as.cmdmode = cmd; // the look-ahead may land on a mnemonic
        int clex   = next_token(&cval);
        as.cmdmode = 0;

        int prec = precedence(clex);
        if (prec < minprec) {
            // Not an operator we may consume: the expression ends here (or
            // belongs to an outer level).  Put the token back for the caller.
            unget_token(clex, cval);
            return lseg;
        }
        rseg = parse_term();
        rhs  = as.intval; // capture before the recursion clobbers as.intval
        rseg = parse_binary(&rhs, rseg, prec + 1, cmd);
        lseg = apply_op(clex, lhs, lseg, rhs, rseg);
    }
}

//
// Parse a whole expression and return its value (low 24 bits; the full 48-bit
// value stays in as.intval); the base segment number is stored in *s.
//
//   expression = [term {operation term}...]
//   term       = {"-" | "~" | "+"} term | operand
//   operand    = LNAME | LNUM | "." | "(" expression ")" | "{" expression "}"
//   operation  = "+" | "-" | "&" | "|" | "^" | "~" | "<<" | ">>" | "/" | "*" | "%"
//
// Operators bind with the usual C precedence and associate left to right; see
// precedence().  An expression as a whole may be empty (an omitted operand
// field is 0, absolute), but an operator's right operand may not - the empty
// case is recognized here and nowhere else, so "1 + * 2" stays an error.
//
// cmdmode is saved and cleared so that operand characters are not mistaken for
// parts of a mnemonic, then restored around the look-aheads that may
// legitimately land on a following instruction.
//
long parse_expr(int *s)
{
    int clex, cval, seg;
    int64_t rez;
    int cmd    = as.cmdmode; // a machine instruction may follow the expression
    as.cmdmode = 0;          // operands themselves never absorb operator chars

    // look at the first token
    switch (clex = next_token(&cval)) {
    default:
        // No leading operand (e.g. an empty operand field): value is 0,
        // absolute.  Put the token back for the caller.
        unget_token(clex, cval);
        as.cmdmode = cmd; // restore for the caller
        as.intval  = 0;
        *s         = SABS;
        return 0;
    case LNUM:
    case LNAME:
    case '.':
    case '(':
    case '{':
    case '-': // a leading unary prefix does start an expression
    case '~':
    case '+':
        unget_token(clex, cval);
        break;
    }
    seg = parse_term();
    rez = as.intval;
    seg = parse_binary(&rez, seg, 1, cmd);

    as.cmdmode = cmd; // restore for the caller
    as.intval  = rez;
    *s         = seg;
    return rez & HALF_MASK;
}
