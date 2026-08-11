%{
/*
 * expr -- evaluate arguments as an expression
 *
 *	status returns:
 *		0 - the expression is neither null nor `0'
 *		1 - the expression is null or `0'
 *		2 - the expression is invalid
 *
 * Task C11.  README.md beside this file is the account; what follows is only what a
 * reader of the diff needs.
 *
 * YYSTYPE IS A MACRO, NOT A TYPE NAME, hence the typedef: `#define YYSTYPE char *'
 * would make yyval a plain char, and an int cannot hold a fat pointer at all.
 *
 * THE CHARACTER CLASS IS 256 BITS, not v7's 128, as grep's and sed's are.  This copy
 * of the engine is the only one with an interval repeat, so the width appears in
 * eight places rather than five.
 *
 * FOUR DELIBERATE DIVERGENCES, also in expr.1.umm and README.md (../README.md SS10):
 * `<=' really is <= (v7 computed >=); a leading minus makes a number on both sides of
 * a relation and in arithmetic (v7 allowed it only on a relation's left operand); a
 * paren is an operator only when it is the whole argument; and division by zero is
 * diagnosed rather than left to the machine, which faults on it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char *charptr;
#define YYSTYPE charptr

// One compiled expression.  256 in v7, whose bound test named &expbuf[512].
#define ESIZE 512

// One character class, in bytes: 256 bits, so `c >> 3' lands in 0..31 by
// construction and the match side needs no mask.  cmd/sed/sed.h says it at length.
#define CCLSIZE 32
%}

%token OR AND ADD SUBT MULT DIV REM EQ GT GEQ LT LEQ NEQ
%token A_STRING SUBSTR LENGTH INDEX NOARG MATCH

/* operators listed below in increasing precedence: */
%left OR
%left AND
%left EQ LT GT GEQ LEQ NEQ
%left ADD SUBT
%left MULT DIV REM
%left MCH
%left MATCH
%left SUBSTR
%left LENGTH INDEX
%%

/* a single `expression' is evaluated and printed: */

expression:	expr NOARG = {
			printf("%s\n", $1);
			exit(istrue($1) ? 0 : 1);
			}
	;

expr: '(' expr ')'          = { $$ = $2; }
	| expr OR expr          = { $$ = conj(OR, $1, $3); }
	| expr AND expr         = { $$ = conj(AND, $1, $3); }
	| expr EQ expr          = { $$ = rel(EQ, $1, $3); }
	| expr GT expr          = { $$ = rel(GT, $1, $3); }
	| expr GEQ expr         = { $$ = rel(GEQ, $1, $3); }
	| expr LT expr          = { $$ = rel(LT, $1, $3); }
	| expr LEQ expr         = { $$ = rel(LEQ, $1, $3); }
	| expr NEQ expr         = { $$ = rel(NEQ, $1, $3); }
	| expr ADD expr         = { $$ = arith(ADD, $1, $3); }
	| expr SUBT expr        = { $$ = arith(SUBT, $1, $3); }
	| expr MULT expr        = { $$ = arith(MULT, $1, $3); }
	| expr DIV expr         = { $$ = arith(DIV, $1, $3); }
	| expr REM expr         = { $$ = arith(REM, $1, $3); }
	| expr MCH expr	        = { $$ = match($1, $3); }
	| MATCH expr expr       = { $$ = match($2, $3); }
	| SUBSTR expr expr expr = { $$ = substr($2, $3, $4); }
	| LENGTH expr           = { $$ = length($2); }
	| INDEX expr expr       = { $$ = idx($2, $3); }
	| A_STRING
	;
%%

#define EQL(x, y) (strcmp(x, y) == 0)

#define NUMLEN 16 // thirteen digits, a sign and a NUL

static char **Av;
static int Ac;
static int Argi;

static void reerror(void);
static int advance(char *lp, char *ep);

// A literal prefix, not argv[0]: under b6sim that is the staged absolute path.
void yyerror(char *s)
{
	fprintf(stderr, "expr: %s\n", s);
	exit(2);
}

static int istrue(char *s)
{
	return !EQL(s, "0") && !EQL(s, "");
}

static char *numstr(int v)
{
	char *rv = malloc(NUMLEN);

	if (rv == 0)
		yyerror("out of space");
	sprintf(rv, "%d", v);
	return rv;
}

// An optional minus and one or more digits.  The null string is not a number.
static int isnum(const char *s)
{
	if (*s == '-')
		++s;
	if (*s == 0)
		return 0;
	while (*s) {
		if (*s < '0' || *s > '9')
			return 0;
		++s;
	}
	return 1;
}

// ---- v7's regexp(3), inlined as v7 inlined it.  cmd/sed/sed.h is the twin. -------

#define CBRA  2
#define CCHR  4
#define CDOT  8
#define CCL   12
#define CDOL  20
#define CEOF  22
#define CKET  24
#define CBACK 36

#define STAR 01
#define RNGE 03

#define NBRA 9

#define PLACE(c)   ep[(c) >> 3] |= bittab[(c) & 07]
#define ISTHERE(c) (ep[(c) >> 3] & bittab[(c) & 07])

// Depth is exactly the star count; the 4,096-word stack is checked by nothing.
#define MAXDEPTH 12

static char *braslist[NBRA];
static char *braelist[NBRA];
static int nbra;
static char *loc2;
static char *matched;
static int depth;

static char bittab[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static void reerror(void)
{
	yyerror("RE error");
}

#define INIT      char *sp = instring;
#define GETC()    (*sp++)
#define PEEKC()   (*sp)
#define UNGETC(c) (--sp)

static void compile(char *instring, char *ep, char *endbuf, int seof)
{
	INIT /* Dependent declarations and initializations */
	int c;
	int eof = seof;
	char *lastep;
	int cclcnt;
	char bracket[NBRA], *bracketp;
	int closed;
	int neg;
	int lc;
	int i, cflg;

	lastep = 0;
	// v7 also tested *ep, a static buffer the previous call had filled.
	if ((c = GETC()) == eof)
		reerror();
	bracketp = bracket;
	closed = nbra = 0;
	if (c != '^')
		UNGETC(c);
	for (;;) {
		if (ep >= endbuf)
			reerror();
		if ((c = GETC()) != '*' && ((c != '\\') || (PEEKC() != '{')))
			lastep = ep;
		if (c == eof) {
			*ep++ = CEOF;
			return;
		}
		switch (c) {

		case '.':
			*ep++ = CDOT;
			continue;

		case '\n':
			reerror();
			continue;

		case '*':
			if (lastep == 0 || *lastep == CBRA || *lastep == CKET)
				goto defchar;
			*lastep |= STAR;
			continue;

		case '$':
			if (PEEKC() != eof)
				goto defchar;
			*ep++ = CDOL;
			continue;

		case '[':
			if (&ep[CCLSIZE + 1] >= endbuf)
				reerror();

			*ep++ = CCL;
			lc = 0;
			memset(ep, 0, CCLSIZE);

			neg = 0;
			if ((c = GETC()) == '^') {
				neg = 1;
				c = GETC();
			}

			do {
				if (c == '\0' || c == '\n')
					reerror();
				if (c == '-' && lc != 0) {
					if ((c = GETC()) == ']') {
						PLACE('-');
						break;
					}
					while (lc < c) {
						PLACE(lc);
						lc++;
					}
				}
				lc = c;
				PLACE(c);
			} while ((c = GETC()) != ']');
			if (neg) {
				for (cclcnt = 0; cclcnt < CCLSIZE; cclcnt++)
					ep[cclcnt] ^= 0377; // v7 wrote -1, into a signed char
				ep[0] &= 0376;             // a NUL is never in a class
			}

			ep += CCLSIZE;

			continue;

		case '\\':
			switch (c = GETC()) {

			case '(':
				if (nbra >= NBRA)
					reerror();
				*bracketp++ = nbra;
				*ep++ = CBRA;
				*ep++ = nbra++;
				continue;

			case ')':
				if (bracketp <= bracket)
					reerror();
				*ep++ = CKET;
				*ep++ = *--bracketp;
				closed++;
				continue;

			case '{':
				if (lastep == 0)
					goto defchar;
				*lastep |= RNGE;
				cflg = 0;
			nlim:
				c = GETC();
				i = 0;
				do {
					if ('0' <= c && c <= '9')
						i = 10 * i + c - '0';
					else
						reerror();
				} while (((c = GETC()) != '\\') && (c != ','));
				if (i > 255)
					reerror();
				*ep++ = i;
				if (c == ',') {
					if (cflg++)
						reerror();
					if ((c = GETC()) == '\\')
						*ep++ = 255;
					else {
						UNGETC(c);
						goto nlim; /* get 2'nd number */
					}
				}
				if (GETC() != '}')
					reerror();
				if (!cflg) /* one number */
					*ep++ = i;
				else if (ep[-1] < ep[-2]) // v7 masked both with 0377
					reerror();
				continue;

			case '\n':
				reerror();
				continue;

			case 'n':
				c = '\n';
				goto defchar;

			default:
				if (c >= '1' && c <= '9') {
					if ((c -= '1') >= closed)
						reerror();
					*ep++ = CBACK;
					*ep++ = c;
					continue;
				}
			}
			/* Drop through to default to use \ to turn off special chars */

		defchar:
		default:
			lastep = ep;
			*ep++ = CCHR;
			*ep++ = c;
		}
	}
}

// low and size were globals; v7's & 0377 is a no-op on an unsigned char.
static void getrnge(char *str, int *low, int *size)
{
	*low  = *str++;
	*size = *str == 255 ? 20000 : *str - *low;
}

static int ecmp(char *a, char *b, int count)
{
	// Two fat pointers, and == is a raw word compare -- correct here.
	if (a == b) /* should have been caught in compile() */
		reerror();
	while (count--)
		if (*a++ != *b++)
			return 0;
	return 1;
}

static int match_re(char *lp, char *ep)
{
	char *curlp;
	int c;
	char *bbeg;
	int ct;
	int low, size;

	for (;;)
		switch (*ep++) {

		case CCHR:
			if (*ep++ == *lp++)
				continue;
			return 0;

		case CDOT:
			if (*lp++)
				continue;
			return 0;

		case CDOL:
			if (*lp == 0)
				continue;
			return 0;

		case CEOF:
			loc2 = lp;
			return 1;

		case CCL:
			c = *lp++; // v7 masked with 0177 into a 128-bit table
			if (ISTHERE(c)) {
				ep += CCLSIZE;
				continue;
			}
			return 0;

		case CBRA:
			braslist[(int)*ep++] = lp;
			continue;

		case CKET:
			braelist[(int)*ep++] = lp;
			continue;

		case CCHR | RNGE:
			c = *ep++;
			getrnge(ep, &low, &size);
			while (low--)
				if (*lp++ != c)
					return 0;
			curlp = lp;
			while (size--)
				if (*lp++ != c)
					break;
			if (size < 0)
				lp++;
			ep += 2;
			goto star;

		case CDOT | RNGE:
			getrnge(ep, &low, &size);
			while (low--)
				if (*lp++ == '\0')
					return 0;
			curlp = lp;
			while (size--)
				if (*lp++ == '\0')
					break;
			if (size < 0)
				lp++;
			ep += 2;
			goto star;

		case CCL | RNGE:
			getrnge(ep + CCLSIZE, &low, &size);
			while (low--) {
				c = *lp++;
				if (!ISTHERE(c))
					return 0;
			}
			curlp = lp;
			while (size--) {
				c = *lp++;
				if (!ISTHERE(c))
					break;
			}
			if (size < 0)
				lp++;
			ep += CCLSIZE + 2;
			goto star;

		case CBACK:
			bbeg = braslist[(int)*ep];
			ct   = braelist[(int)*ep++] - bbeg;

			if (ecmp(bbeg, lp, ct)) {
				lp += ct;
				continue;
			}
			return 0;

		case CBACK | STAR:
			bbeg  = braslist[(int)*ep];
			ct    = braelist[(int)*ep++] - bbeg;
			curlp = lp;
			while (ecmp(bbeg, lp, ct))
				lp += ct;

			while (lp >= curlp) {
				if (advance(lp, ep))
					return 1;
				lp -= ct;
			}
			return 0;

		case CDOT | STAR:
			curlp = lp;
			while (*lp++)
				;
			goto star;

		case CCHR | STAR:
			curlp = lp;
			while (*lp++ == *ep)
				;
			ep++;
			goto star;

		case CCL | STAR:
			curlp = lp;
			do {
				c = *lp++;
			} while (ISTHERE(c));
			ep += CCLSIZE;
			goto star;

		star:
			do {
				--lp; // v7 also tested `== locs', a null pointer
				if (advance(lp, ep))
					return 1;
			} while (lp > curlp);
			return 0;

		default:
			// v7 had none, so a bad opcode walked ep forward silently.
			yyerror("RE botch");
		}
}

static int advance(char *lp, char *ep)
{
	int r;

	if (++depth > MAXDEPTH)
		yyerror("expression too complex");
	r = match_re(lp, ep);
	--depth;
	return r;
}

// Returns the number of BYTES matched, leaving the \( \) capture in `matched'.
static int ematch(char *s, char *p)
{
	static char expbuf[ESIZE];
	char *q;
	int num;

	matched = "";
	compile(p, expbuf, &expbuf[ESIZE], 0);
	if (nbra > 1)
		yyerror("too many \\(");
	depth = 0;
	if (advance(s, expbuf)) {
		if (nbra == 1) {
			q   = braslist[0];
			num = braelist[0] - q;
			// v7 copied this into a char[1][128], with no bound on num.
			matched = malloc(num + 1);
			if (matched == 0)
				yyerror("out of space");
			strncpy(matched, q, num);
			matched[num] = '\0';
		}
		return loc2 - s;
	}
	return 0;
}

// ---- the operators ----------------------------------------------------------------

static char *rel(int op, char *r1, char *r2)
{
	int i;

	if (isnum(r1) && isnum(r2)) {
		int a = atoi(r1), b = atoi(r2);

		i = a < b ? -1 : a > b; // three-way; a - b can overflow a word
	} else
		i = strcmp(r1, r2);

	switch (op) {
	case EQ:
		i = i == 0;
		break;
	case GT:
		i = i > 0;
		break;
	case GEQ:
		i = i >= 0;
		break;
	case LT:
		i = i < 0;
		break;
	case LEQ:
		i = i <= 0; // v7 wrote i >= 0, which is GEQ
		break;
	case NEQ:
		i = i != 0;
		break;
	}
	return i ? "1" : "0";
}

static char *arith(int op, char *r1, char *r2)
{
	int i1, i2;

	if (!isnum(r1) || !isnum(r2))
		yyerror("non-numeric argument");
	i1 = atoi(r1);
	i2 = atoi(r2);

	switch (op) {
	case ADD:
		i1 = i1 + i2;
		break;
	case SUBT:
		i1 = i1 - i2;
		break;
	case MULT:
		i1 = i1 * i2;
		break;
	case DIV:
	case REM:
		if (i2 == 0) // the divide instruction faults on it
			yyerror("division by zero");
		i1 = op == DIV ? i1 / i2 : i1 % i2;
		break;
	}
	return numstr(i1);
}

static char *conj(int op, char *r1, char *r2)
{
	if (op == OR)
		return istrue(r1) ? r1 : istrue(r2) ? r2 : "0";
	return istrue(r1) && istrue(r2) ? r1 : "0";
}

// Indices and a copy: v7 walked with `while(--si)', which does not terminate for
// si <= 0, and then stored a NUL through its first argument -- sometimes a literal.
static char *substr(char *v, char *s, char *w)
{
	int si  = atoi(s);
	int wi  = atoi(w);
	int len = strlen(v);
	char *rv;

	if (si < 1 || si > len || wi <= 0)
		return "";
	--si;
	if (wi > len - si)
		wi = len - si;
	rv = malloc(wi + 1);
	if (rv == 0)
		yyerror("out of space");
	strncpy(rv, v + si, wi);
	rv[wi] = '\0';
	return rv;
}

static char *length(char *s)
{
	return numstr((int)strlen(s));
}

// Not index(): libc has index(3) and execvp() calls it, so a second definition here
// would be resolved by whichever b6ld saw first, with a diagnostic from nothing.
static char *idx(char *s, char *t)
{
	int i, j;

	for (i = 0; s[i]; ++i)
		for (j = 0; t[j]; ++j)
			if (s[i] == t[j])
				return numstr(i + 1);
	return "0";
}

static char *match(char *s, char *p)
{
	int n = ematch(s, p);

	// A \( \) group replaces the count, and is the null string on no match.
	return nbra ? matched : numstr(n);
}

// ---- the lexer, which reads the argument list -------------------------------------

static const char *const opname[] = { "|",  "&",  "+",	 "-",	  "*",	   "/",	  "%",
				      ":",  "=",  "==",	 "<",	  "<=",	   ">",	  ">=",
				      "!=", "match", "substr", "length", "index", 0 };
static const int optok[] = { OR,   AND, ADD,	SUBT,	MULT,  DIV, REM,
			     MCH,  EQ,	EQ,	LT,	LEQ,   GT,  GEQ,
			     NEQ,  MATCH, SUBSTR, LENGTH, INDEX };

int yylex(void)
{
	char *p;
	int i;

	if (Argi >= Ac)
		return NOARG;

	p = Av[Argi++];

	if (EQL(p, "(") || EQL(p, ")")) // v7 tested *p, so `(3)' was a paren
		return p[0];

	for (i = 0; opname[i]; ++i)
		if (EQL(opname[i], p))
			return optok[i];

	yylval = p;
	return A_STRING;
}

int main(int argc, char **argv)
{
	Ac   = argc;
	Argi = 1;
	Av   = argv;
	yyparse();
	return 0; // not reached: the accepting action exits
}
