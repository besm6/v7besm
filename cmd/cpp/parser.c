#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

#define YYSTYPE int

void advance(void);
int eval_expr(void);
int eval_term(void);

void advance(void)
{
    cpp.look_token = lex_if_token();
    cpp.look_value = cpp.tok_value;
}

int match(int token)
{
    if (cpp.look_token == token) {
        advance();
        return 1;
    }
    return 0;
}

// Precedence and associativity handling
int precedence(int token)
{
    switch (token) {
    case ',':
        return 1;
    case '=':
        return 2;
    case '?':
    case ':':
        return 3;
    case OROR:
        return 4;
    case ANDAND:
        return 5;
    case '|':
    case '^':
        return 6;
    case '&':
        return 7;
    case EQ:
    case NE:
        return 8;
    case '<':
    case '>':
    case LE:
    case GE:
        return 9;
    case LS:
    case RS:
        return 10;
    case '+':
    case '-':
        return 11;
    case '*':
    case '/':
    case '%':
        return 12;
    case '!':
    case '~':
    case UMINUS:
        return 13;
    case '(':
    case '.':
        return 14;
    default:
        return 0;
    }
}

int eval_expr(void)
{
    int val, val2;

    val = eval_term();

    while (1) {
        int op      = cpp.look_token;
        int op_prec = precedence(op);

        if (op_prec <= precedence(',')) { // Handle lowest precedence up to comma
            if (op == '*' && match('*')) {
                val2 = eval_expr();
                val  = val * val2;
            } else if (op == '/' && match('/')) {
                val2 = eval_expr();
                if (val2 == 0)
                    pperror("Division by zero");
                else
                    val = val / val2;
            } else if (op == '%' && match('%')) {
                val2 = eval_expr();
                if (val2 == 0)
                    pperror("Modulo by zero");
                else
                    val = val % val2;
            } else if (op == '+' && match('+')) {
                val2 = eval_expr();
                val  = val + val2;
            } else if (op == '-' && match('-')) {
                val2 = eval_expr();
                val  = val - val2;
            } else if (op == LS && match(LS)) {
                val2 = eval_expr();
                val  = val << val2;
            } else if (op == RS && match(RS)) {
                val2 = eval_expr();
                val  = val >> val2;
            } else if (op == '<' && match('<')) {
                val2 = eval_expr();
                val  = val < val2;
            } else if (op == '>' && match('>')) {
                val2 = eval_expr();
                val  = val > val2;
            } else if (op == LE && match(LE)) {
                val2 = eval_expr();
                val  = val <= val2;
            } else if (op == GE && match(GE)) {
                val2 = eval_expr();
                val  = val >= val2;
            } else if (op == EQ && match(EQ)) {
                val2 = eval_expr();
                val  = val == val2;
            } else if (op == NE && match(NE)) {
                val2 = eval_expr();
                val  = val != val2;
            } else if (op == '&' && match('&')) {
                val2 = eval_expr();
                val  = val & val2;
            } else if (op == '^' && match('^')) {
                val2 = eval_expr();
                val  = val ^ val2;
            } else if (op == '|' && match('|')) {
                val2 = eval_expr();
                val  = val | val2;
            } else if (op == ANDAND && match(ANDAND)) {
                val2 = eval_expr();
                val  = val && val2;
            } else if (op == OROR && match(OROR)) {
                val2 = eval_expr();
                val  = val || val2;
            } else if (op == '?' && match('?')) {
                val2 = eval_expr();
                if (!match(':'))
                    pperror("Expected ':' in ternary operator");
                int val3 = eval_expr();
                val      = val ? val2 : val3;
            } else if (op == ',' && match(',')) {
                val2 = eval_expr();
                val  = val2;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return val;
}

int eval_term(void)
{
    int val;

    if (match('-')) {
        val = eval_term();
        return -val;
    } else if (match('!')) {
        val = eval_term();
        return !val;
    } else if (match('~')) {
        val = eval_term();
        return ~val;
    } else if (match('(')) {
        val = eval_expr();
        if (!match(')'))
            pperror("Expected ')'");
        return val;
    } else if (match(DEFINED)) {
        if (match('(')) {
            if (cpp.look_token != number)
                pperror("Expected number in DEFINED");
            val = cpp.look_value;
            advance();
            if (!match(')'))
                pperror("Expected ')' in DEFINED");
            return val;
        } else if (cpp.look_token == number) {
            val = cpp.look_value;
            advance();
            return val;
        } else {
            pperror("Expected number or '(' after DEFINED");
        }
    } else if (cpp.look_token == number) {
        val = cpp.look_value;
        advance();
        return val;
    }

    pperror("Invalid term");
    return 0;
}

int eval_if(void)
{
    advance();
    int result = eval_expr();
    if (cpp.look_token != stop)
        pperror("Expected stop token");
    return result;
}
