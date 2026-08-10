%{
//
// calct -- a desk calculator that exists to be a yacc-generated parser.
// NOT A PROGRAM ON THE IMAGE.  ../README.md, "The native case".
//
// It exercises the four things the port rewrote: a reduction stack deeper than
// one, precedence, `error' recovery (which pops to the sentinel), and eight-bit
// input.  yylex() and yyerror() have the signatures y2.c emits.
//
#include <stdio.h>

static int line  = 1;
static int errs  = 0;
static int lastc = 0;   // the byte yylex handed over last, reported by yyerror
%}

%token NUMBER
%left '+' '-'
%left '*' '/'
%right UMINUS

%%

list    :
        | list stat '\n'
        | list error '\n'       { yyerrok; }
        ;

stat    : expr                  { printf("%d\n", $1); }
        ;

expr    : expr '+' expr         { $$ = $1 + $3; }
        | expr '-' expr         { $$ = $1 - $3; }
        | expr '*' expr         { $$ = $1 * $3; }
        | expr '/' expr         { $$ = $3 == 0 ? 0 : $1 / $3; }
        | '-' expr %prec UMINUS { $$ = -$2; }
        | '(' expr ')'          { $$ = $2; }
        | NUMBER
        ;

%%

// Reads bytes, not characters: a byte above 0177 is a value in 128..255.
int yylex(void)
{
    int c;

    for (;;) {
        c = getchar();
        if (c == EOF)
            return 0;
        if (c == ' ' || c == '\t')
            continue;
        break;
    }
    lastc = c;

    if (c >= '0' && c <= '9') {
        yylval = 0;
        do {
            yylval = yylval * 10 + (c - '0');
            c = getchar();
        } while (c >= '0' && c <= '9');
        if (c != EOF)
            ungetc(c, stdin);
        return NUMBER;
    }

    if (c == '\n')
        ++line;
    return c;
}

// The byte is reported by value, which is what makes the eightbit case an
// assertion: `Ы' is D0 AB, and 0253 masked to seven bits is `+'.
void yyerror(char *s)
{
    ++errs;
    printf("%s, line %d, byte %d\n", s, line, lastc);
}

int main(void)
{
    yyparse();
    printf("%d errors\n", errs);
    return errs != 0;
}
