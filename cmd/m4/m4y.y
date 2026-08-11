%{
/*
 * m4 -- the expression parser behind the `eval' builtin.
 *
 * Task C13.  README.md beside this file is the account.
 *
 * YYSTYPE IS ONE MACHINE WORD.  v7 wrote `long' for the PDP-11's sake; a long is an
 * int is one 41-bit word here, so eval is wider than the manual page's "32-bit
 * arithmetic" and the page says so now.
 *
 * DIVISION BY ZERO IS DIAGNOSED.  The divide instruction faults on it, so v7's
 * unchecked `/' and `%' killed the process.  The error rides out on evalerr and
 * YYABORT, which the skeleton defines as `return (1)' -- the value doeval() already
 * treated as failure, so the path is v7's own.
 *
 * The value channel is a global rather than yylval: yylex() sets evalval and the
 * DIGITS rule reads it back.  That is v7's, and it works only because the state
 * reached by shifting DIGITS reduces by default without a lookahead.
 */
extern int evalval;
extern char *pe;
extern char *evalerr;

#define YYSTYPE int

static int peek(int c, int r1, int r2);
%}

%term DIGITS
%left '|'
%left '&'
%right '!'
%nonassoc GT GE LT LE NE EQ
%left '+' '-'
%left '*' '/' '%'
%right POWER
%right UMINUS
%%

s	: e	={ evalval = $1; }
	|	={ evalval = 0; }
	;

e	: e '|' e	={ $$ = ($1!=0 || $3!=0) ? 1 : 0; }
	| e '&' e	={ $$ = ($1!=0 && $3!=0) ? 1 : 0; }
	| '!' e		={ $$ = $2 == 0; }
	| e EQ e	={ $$ = $1 == $3; }
	| e NE e	={ $$ = $1 != $3; }
	| e GT e	={ $$ = $1 > $3; }
	| e GE e	={ $$ = $1 >= $3; }
	| e LT e	={ $$ = $1 < $3; }
	| e LE e	={ $$ = $1 <= $3; }
	| e '+' e	={ $$ = ($1+$3); }
	| e '-' e	={ $$ = ($1-$3); }
	| e '*' e	={ $$ = ($1*$3); }
	| e '/' e	={ if ($3 == 0) { evalerr = "divide by zero"; YYABORT; }
			   $$ = ($1/$3); }
	| e '%' e	={ if ($3 == 0) { evalerr = "divide by zero"; YYABORT; }
			   $$ = ($1%$3); }
	| '(' e ')'	={ $$ = ($2); }
	| e POWER e	={ for ($$=1; $3-->0; $$ *= $1); }
	| '-' e %prec UMINUS	={ $$ = -$2; }
	| '+' e %prec UMINUS	={ $$ = $2; }
	| DIGITS	={ $$ = evalval; }
	;

%%

int yylex(void)
{
	while (*pe==' ' || *pe=='\t' || *pe=='\n')
		pe++;
	switch(*pe) {
	// End of the expression: do not step past the NUL, yylex being callable again.
	case '\0':
		return(0);
	case '+':
	case '-':
	case '/':
	case '%':
	case '(':
	case ')':
		return(*pe++);
	case '^':
		pe++;
		return(POWER);
	case '*':
		return(peek('*', POWER, '*'));
	case '>':
		return(peek('=', GE, GT));
	case '<':
		return(peek('=', LE, LT));
	case '=':
		return(peek('=', EQ, EQ));
	case '|':
		return(peek('|', '|', '|'));
	case '&':
		return(peek('&', '&', '&'));
	case '!':
		return(peek('=', NE, '!'));
	default:
		evalval = 0;
		while (*pe >= '0' && *pe <= '9')
			evalval = evalval*10 + *pe++ - '0';
		return(DIGITS);
	}
}

static int peek(int c, int r1, int r2)
{
	if (*++pe != c)
		return(r2);
	++pe;
	return(r1);
}

// A no-op, as v7's is: doeval() prints the message, so a syntax error keeps v7's wording.
void yyerror(char *s)
{
}
