#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

//
// Evaluator for the constant expression on a "#if" line.  The scanner
// (lex_if_token, in yylex.c) turns the line into a stream of tokens; this file
// reads them one at a time and computes the integer result, which the directive
// loop treats as true (nonzero) or false.  It is a small recursive-descent
// parser with one-token lookahead kept in cpp.look_token / cpp.look_value.
//

#define YYSTYPE int // (historical) the value type carried by a token: plain int

void advance(void);
int eval_expr(void);
int eval_binary(int min_prec);
int eval_term(void);

//
// Consume the current token and fetch the next one into the lookahead slot.
//
void advance(void)
{
    cpp.look_token = lex_if_token();
    cpp.look_value = cpp.tok_value;
}

//
// If the lookahead token is "token", consume it and return 1 (true); otherwise
// leave it in place and return 0.
//
int match(int token)
{
    if (cpp.look_token == token) {
        advance();
        return 1;
    }
    return 0;
}

//
// Return the binding strength of an operator: bigger means it binds tighter
// (e.g. '*' beats '+').  Used to decide the order operators are applied in.
//
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

//
// Apply a binary operator "op" to two already-evaluated operands and return the
// result.  Division/modulo by zero is reported (and yields 0); an operator token
// with no arithmetic meaning here (e.g. a stray '.') is an error -- which is how
// a malformed operand such as a floating constant gets diagnosed.
//
static int apply_op(int op, int a, int b)
{
    switch (op) {
    case '*':
        return a * b;
    case '/':
        if (b == 0) {
            pperror("Division by zero");
            return 0;
        }
        return a / b;
    case '%':
        if (b == 0) {
            pperror("Modulo by zero");
            return 0;
        }
        return a % b;
    case '+':
        return a + b;
    case '-':
        return a - b;
    case LS:
        return a << b;
    case RS:
        return a >> b;
    case '<':
        return a < b;
    case '>':
        return a > b;
    case LE:
        return a <= b;
    case GE:
        return a >= b;
    case EQ:
        return a == b;
    case NE:
        return a != b;
    case '&':
        return a & b;
    case '^':
        return a ^ b;
    case '|':
        return a | b;
    case ANDAND:
        return a && b;
    case OROR:
        return a || b;
    case ',':
        return b;
    default:
        pperror("Unexpected operator in preprocessor if");
        return a;
    }
}

//
// Parse and evaluate an expression using precedence climbing: an operand
// (eval_term) followed by any number of "operator operand" pairs, where the
// operand of each operator is parsed only as far as operators that bind at least
// as tightly.  This honors both operator precedence (from precedence()) and
// left-to-right associativity, and it handles the right-associative ?: ternary.
// "min_prec" is the lowest precedence this call is allowed to consume.  Returns
// the computed value.
//
int eval_binary(int min_prec)
{
    int val = eval_term();

    for (;;) {
        int op   = cpp.look_token;
        int prec = precedence(op);

        if (prec == 0 || prec < min_prec)
            break;
        if (op == ':') // belongs to an enclosing '?', not a binary operator here
            break;

        advance(); // consume the operator

        if (op == '?') {
            // Ternary conditional (right-associative): middle runs up to ':'.
            int mid = eval_binary(precedence(','));
            if (!match(':'))
                pperror("Expected ':' in ternary operator");
            int els = eval_binary(prec);
            val     = val ? mid : els;
        } else if (op == '=') {
            // Assignment is a constraint violation in a constant expression.
            pperror("Assignment operator not allowed in preprocessor if");
            eval_binary(prec); // consume the right-hand side to stay in sync
        } else {
            // Left-associative binary operator: the right operand may only
            // absorb operators that bind strictly tighter.
            int rhs = eval_binary(prec + 1);
            val     = apply_op(op, val, rhs);
        }
    }

    return val;
}

//
// Parse and evaluate a whole #if expression (down to the comma operator).
//
int eval_expr(void)
{
    return eval_binary(precedence(','));
}

//
// Parse and evaluate a single operand (a "term"): an optional unary operator
// (-, !, ~) applied to another term, a parenthesized expression, a "defined(X)"
// test, or a plain number.  Returns its value.
//
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

//
// Entry point called from the directive loop for a "#if" line: prime the
// lookahead, evaluate the whole expression, and check that the line ended where
// expected.  Returns the expression's value (nonzero = take the #if branch).
//
int eval_if(void)
{
    advance();
    int result = eval_expr();
    if (cpp.look_token != stop)
        pperror("Expected stop token");
    return result;
}
