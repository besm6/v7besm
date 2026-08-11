%{
//
// calcu -- calct with a %union, so the three aggregate copies in ../../yaccpar.c
// are compiled and run.  NOT A PROGRAM ON THE IMAGE.  ../README.md, "The contract".
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVAR 8

static char *vname[NVAR];       // variable names, strdup'd by yylex
static int vval[NVAR];          // and their values
static int nvar  = 0;
static int errs  = 0;
static int line  = 1;

// The slot for s, created on first mention; NULL when the table is full.
static int *cell(char *s)
{
    int i;

    for (i = 0; i < nvar; i++)
        if (strcmp(vname[i], s) == 0)
            return &vval[i];
    if (nvar >= NVAR)
        return NULL;
    vname[nvar] = s;
    vval[nvar]  = 0;
    return &vval[nvar++];
}
%}

%union
    {
    int i;
    char *s;
    }

%token <i> NUMBER
%token <s> NAME

%type <i> stat expr
%type <s> lhs

%left '+' '-'
%left '*' '/'
%right UMINUS

%%

list    :
        | list stat '\n'
        | list error '\n'       { yyerrok; }
        ;

stat    : expr                  { printf("%d\n", $1); $$ = $1; }
        | lhs '=' expr          {
                                int *p = cell($1);
                                if (p == NULL) {
                                    printf("too many variables\n");
                                    $$ = 0;
                                } else {
                                    *p  = $3;
                                    $$  = $3;
                                    printf("%s = %d\n", $1, $3);
                                }
                                }
        ;

lhs     : NAME                  { $$ = $1; }
        ;

expr    : expr '+' expr         { $$ = $1 + $3; }
        | expr '-' expr         { $$ = $1 - $3; }
        | expr '*' expr         { $$ = $1 * $3; }
        | expr '/' expr         { $$ = $3 == 0 ? 0 : $1 / $3; }
        | '-' expr %prec UMINUS { $$ = -$2; }
        | '(' expr ')'          { $$ = $2; }
        | NAME                  {
                                int *p = cell($1);
                                $$ = p == NULL ? 0 : *p;
                                }
        | NUMBER
        ;

%%

// The name is strdup'd, so the char * on the value stack outlives the buffer.
int yylex(void)
{
    static char buf[32];
    int c, n;

    for (;;) {
        c = getchar();
        if (c == EOF)
            return 0;
        if (c == ' ' || c == '\t')
            continue;
        break;
    }

    if (c >= '0' && c <= '9') {
        yylval.i = 0;
        do {
            yylval.i = yylval.i * 10 + (c - '0');
            c = getchar();
        } while (c >= '0' && c <= '9');
        if (c != EOF)
            ungetc(c, stdin);
        return NUMBER;
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        n = 0;
        do {
            if (n < (int)sizeof(buf) - 1)
                buf[n++] = c;
            c = getchar();
        } while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9'));
        if (c != EOF)
            ungetc(c, stdin);
        buf[n] = '\0';
        yylval.s = strdup(buf);
        if (yylval.s == NULL) {
            printf("out of memory\n");
            exit(2);
        }
        return NAME;
    }

    if (c == '\n')
        ++line;
    return c;
}

void yyerror(char *s)
{
    ++errs;
    printf("%s, line %d\n", s, line);
}

int main(void)
{
    yyparse();
    printf("%d errors\n", errs);
    return errs != 0;
}
