//
// Assembler for BESM-6.
// Expression parser.
//
#include <stdio.h>

#include "as.h"

static int getterm(void)
{
    int ty;
    int cval, s;

    switch (getlex(&cval)) {
    default:
        uerror("operand missed");
    case LNUM:
        return SABS;
    case LNAME:
        as.intval.left = as.intval.right = 0;
        ty                         = as.stab[cval].n_type & N_TYPE;
        if (ty == N_UNDF || ty == N_COMM || ty == N_ACOMM) {
            as.extref = cval;
            return SEXT;
        }
        as.intval.right = as.stab[cval].n_value;
        return TYPESEGM(ty);
    case '.':
        as.intval.left  = 0;
        as.intval.right = as.count[as.segm] / 2;
        return as.segm;
    case '(':
        getexpr(&s);
        if (getlex(&cval) != ')')
            uerror("bad () syntax");
        return s;
    case '{':
        // truncate the exponent
        getexpr(&s);
        if (getlex(&cval) != '}')
            uerror("bad () syntax");
        as.intval.left &= 07777777L;
        return s;
    }
}

// long getexpr(int *s) - read an expression.
// Return the value; store the base segment number in *s.
// The low 4 bytes of the value are returned;
// the full copy stays in intval.
//
// expression   = [operand] {operation operand}...
// operand      = LNAME | LNUM | "." | "(" expression ")" | "{" expression "}"
// operation    = "+" | "-" | "&" | "|" | "^" | "~" | "\" | "/" | "*" | "%"
long getexpr(int *s)
{
    int clex;
    int cval, s2;
    struct word rez;

    // look at the first token
    switch (clex = getlex(&cval)) {
    default:
        ungetlex(clex, cval);
        rez.left = rez.right = 0;
        *s                   = SABS;
        break;
    case LNUM:
    case LNAME:
    case '.':
    case '(':
    case '{':
        ungetlex(clex, cval);
        *s  = getterm();
        rez = as.intval;
        break;
    }
    for (;;) {
        switch (clex = getlex(&cval)) {
            long t;
        case '+':
            s2 = getterm();
            if (*s == SABS)
                *s = s2;
            else if (s2 != SABS)
                uerror("too complex expression");
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
            s2 = getterm();
            if (s2 != SABS)
                uerror("too complex expression");
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
        case '&':
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left &= as.intval.left;
            rez.right &= as.intval.right;
            break;
        case '|':
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left |= as.intval.left;
            rez.right |= as.intval.right;
            break;
        case '^':
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left ^= as.intval.left;
            rez.right ^= as.intval.right;
            break;
        case '~':
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left ^= ~as.intval.left;
            rez.right ^= ~as.intval.right;
            break;
        case LLSHIFT: // shift left
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
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
        case LRSHIFT: // shift right
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
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
        case '*': // 31-bit operation
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left = 0;
            rez.right *= as.intval.right;
            break;
        case '/': // 31-bit operation
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left = 0;
            if (as.intval.right)
                rez.right /= as.intval.right;
            else
                uerror("division by zero");
            break;
        case '%': // 31-bit operation
            s2 = getterm();
            if (*s != SABS || s2 != SABS)
                uerror("too complex expression");
            rez.left = 0;
            if (as.intval.right)
                rez.right %= as.intval.right;
            else
                uerror("division (%%) by zero");
            break;
        default:
            ungetlex(clex, cval);
            as.intval = rez;
            return rez.right;
        }
    }
    // NOTREACHED
}
