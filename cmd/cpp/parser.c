#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

#define YYSTYPE int

int yylval;
int lookahead;
int token_val;

void advance(void);
int parse(void);
int e(void);
int term(void);

void advance(void)
{
    lookahead = yylex();
    token_val = yylval;
}

int match(int token)
{
    if (lookahead == token) {
        advance();
        return 1;
    }
    return 0;
}

// Precedence and associativity handling
int prec(int token)
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

int e(void)
{
    int val, val2, val3;
    int op, op_prec;

    val = term();

    while (1) {
        op      = lookahead;
        op_prec = prec(op);

        if (op_prec <= prec(',')) { // Handle lowest precedence up to comma
            if (op == '*' && match('*')) {
                val2 = e();
                val  = val * val2;
            } else if (op == '/' && match('/')) {
                val2 = e();
                if (val2 == 0)
                    pperror("Division by zero");
                val = val / val2;
            } else if (op == '%' && match('%')) {
                val2 = e();
                if (val2 == 0)
                    pperror("Modulo by zero");
                val = val % val2;
            } else if (op == '+' && match('+')) {
                val2 = e();
                val  = val + val2;
            } else if (op == '-' && match('-')) {
                val2 = e();
                val  = val - val2;
            } else if (op == LS && match(LS)) {
                val2 = e();
                val  = val << val2;
            } else if (op == RS && match(RS)) {
                val2 = e();
                val  = val >> val2;
            } else if (op == '<' && match('<')) {
                val2 = e();
                val  = val < val2;
            } else if (op == '>' && match('>')) {
                val2 = e();
                val  = val > val2;
            } else if (op == LE && match(LE)) {
                val2 = e();
                val  = val <= val2;
            } else if (op == GE && match(GE)) {
                val2 = e();
                val  = val >= val2;
            } else if (op == EQ && match(EQ)) {
                val2 = e();
                val  = val == val2;
            } else if (op == NE && match(NE)) {
                val2 = e();
                val  = val != val2;
            } else if (op == '&' && match('&')) {
                val2 = e();
                val  = val & val2;
            } else if (op == '^' && match('^')) {
                val2 = e();
                val  = val ^ val2;
            } else if (op == '|' && match('|')) {
                val2 = e();
                val  = val | val2;
            } else if (op == ANDAND && match(ANDAND)) {
                val2 = e();
                val  = val && val2;
            } else if (op == OROR && match(OROR)) {
                val2 = e();
                val  = val || val2;
            } else if (op == '?' && match('?')) {
                val2 = e();
                if (!match(':'))
                    pperror("Expected ':' in ternary operator");
                val3 = e();
                val  = val ? val2 : val3;
            } else if (op == ',' && match(',')) {
                val2 = e();
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

int term(void)
{
    int val;

    if (match('-')) {
        val = term();
        return -val;
    } else if (match('!')) {
        val = term();
        return !val;
    } else if (match('~')) {
        val = term();
        return ~val;
    } else if (match('(')) {
        val = e();
        if (!match(')'))
            pperror("Expected ')'");
        return val;
    } else if (match(DEFINED)) {
        if (match('(')) {
            if (lookahead != number)
                pperror("Expected number in DEFINED");
            val = token_val;
            advance();
            if (!match(')'))
                pperror("Expected ')' in DEFINED");
            return val;
        } else if (lookahead == number) {
            val = token_val;
            advance();
            return val;
        } else {
            pperror("Expected number or '(' after DEFINED");
        }
    } else if (lookahead == number) {
        val = token_val;
        advance();
        return val;
    }

    pperror("Invalid term");
    return 0;
}

int yyparse(void)
{
    advance();
    int result = e();
    if (lookahead != stop)
        pperror("Expected stop token");
    return result;
}
