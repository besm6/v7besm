/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

%{
// bc(1) -- task C16.  A compiler, not a calculator: it translates this language into
// dc(1) commands and execs /bin/dc to run them.  README.md is the port's account.

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// YYSTYPE IS A MACRO, NOT A TYPE NAME, hence the typedef; and it must be a pointer,
// an int being too narrow for a fat one.  README.md, ../expr/expr.y.
typedef char *charptr;
#define YYSTYPE charptr

#define LIBB "/usr/lib/lib.b"  // the -l math library, staged as a file of its own

#define CARYSZ  1000  // one statement's constants, register names and labels
#define STRSZ   1000  // one statement's quoted strings
#define BSPMAX  3000  // one statement's bundle elements
#define RSTK     256  // deepest bundle nesting routput() will walk
#define NEST      10  // deepest control-structure nesting, v7's bstack[10]
#define NLEV      15  // numb[] must span every level a `break' or `return' can name
#define NFILES    32  // input files named on the command line

_Static_assert(NEST + 2 <= NLEV, "numb[] must reach the innermost level inside a define");

static FILE *inp;                 // v7's `in'
static char cary[CARYSZ], *cp = cary;
static char sary[STRSZ], *sp = sary;  // v7's string[] and str
static int crs  = '0';
static int rcrs = '0';            // reset crs
static int bindx;
static int lev;
static int ln;
static char *ss;
static int bstack[NEST];
static char *numb[NLEV] = { 
	" 0", " 1", " 2", " 3", " 4", " 5", " 6", " 7",
	" 8", " 9", " 10", " 11", " 12", " 13", " 14",
};
static char *pre, *post;
%}
%right '='
%left '+' '-'
%left '*' '/' '%'
%right '^'
%left UMINUS

%term LETTER DIGIT SQRT LENGTH _IF FFF EQ
%term _WHILE _FOR NE LE GE INCR DECR
%term _RETURN _BREAK _DEFINE BASE OBASE SCALE
%term EQPL EQMI EQMUL EQDIV EQREM EQEXP
%term _AUTO DOT
%term QSTR

%%
start	:
	| start stat tail
		= output($2);
	| start def dargs ')' '{' dlist slist '}'
		={ bundle(6, pre, $7, post, "0", numb[lev], "Q");
		   conout($$, $2);
		   rcrs = crs;
		   output("");
		   lev = bindx = 0; }
	;

dlist	: tail
	| dlist _AUTO dlets tail
	;

stat	: e
		={ bundle(2, $1, "ps."); }
	|
		={ bundle(1, ""); }
	| QSTR
		={ bundle(3, "[", $1, "]P"); }
	| LETTER '=' e
		={ bundle(3, $3, "s", $1); }
	| LETTER '[' e ']' '=' e
		={ bundle(4, $6, $3, ":", geta($1)); }
	| LETTER EQOP e
		={ bundle(6, "l", $1, $3, $2, "s", $1); }
	| LETTER '[' e ']' EQOP e
		={ bundle(8, $3, ";", geta($1), $6, $5, $3, ":", geta($1)); }
	| _BREAK
		={ bundle(2, numb[brklev()], "Q"); }
	| _RETURN '(' e ')'
		= bundle(4, $3, post, numb[lev], "Q");
	| _RETURN '(' ')'
		= bundle(4, "0", post, numb[lev], "Q");
	| _RETURN
		= bundle(4, "0", post, numb[lev], "Q");
	| SCALE '=' e
		= bundle(2, $3, "k");
	| SCALE EQOP e
		= bundle(4, "K", $3, $2, "k");
	| BASE '=' e
		= bundle(2, $3, "i");
	| BASE EQOP e
		= bundle(4, "I", $3, $2, "i");
	| OBASE '=' e
		= bundle(2, $3, "o");
	| OBASE EQOP e
		= bundle(4, "O", $3, $2, "o");
	| '{' slist '}'
		={ $$ = $2; }
	| FFF
		={ bundle(1, "fY"); }
	| error
		={ bundle(1, "c"); }
	| _IF CRS BLEV '(' re ')' stat
		={ conout($7, $2);
		   bundle(3, $5, $2, " "); }
	| _WHILE CRS '(' re ')' stat BLEV
		={ bundle(3, $6, $4, $2);
		   conout($$, $2);
		   bundle(3, $4, $2, " "); }
	| fprefix CRS re ';' e ')' stat BLEV
		={ bundle(5, $7, $5, "s.", $3, $2);
		   conout($$, $2);
		   bundle(5, $1, "s.", $3, $2, " "); }
	| '~' LETTER '=' e
		={ bundle(3, $4, "S", $2); }
	;

EQOP	: EQPL
		={ $$ = "+"; }
	| EQMI
		={ $$ = "-"; }
	| EQMUL
		={ $$ = "*"; }
	| EQDIV
		={ $$ = "/"; }
	| EQREM
		={ $$ = "%"; }
	| EQEXP
		={ $$ = "^"; }
	;

fprefix	: _FOR '(' e ';'
		={ $$ = $3; }
	;

BLEV	:
		={ --bindx; }
	;

slist	: stat
	| slist tail stat
		={ bundle(2, $1, $3); }
	;

tail	: '\n'
		={ ln++; }
	| ';'
	;

re	: e EQ e
		= bundle(3, $1, $3, "=");
	| e '<' e
		= bundle(3, $1, $3, ">");
	| e '>' e
		= bundle(3, $1, $3, "<");
	| e NE e
		= bundle(3, $1, $3, "!=");
	| e GE e
		= bundle(3, $1, $3, "!>");
	| e LE e
		= bundle(3, $1, $3, "!<");
	| e
		= bundle(2, $1, " 0!=");
	;

e	: e '+' e
		= bundle(3, $1, $3, "+");
	| e '-' e
		= bundle(3, $1, $3, "-");
	| '-' e		%prec UMINUS
		= bundle(3, " 0", $2, "-");
	| e '*' e
		= bundle(3, $1, $3, "*");
	| e '/' e
		= bundle(3, $1, $3, "/");
	| e '%' e
		= bundle(3, $1, $3, "%");
	| e '^' e
		= bundle(3, $1, $3, "^");
	| LETTER '[' e ']'
		={ bundle(3, $3, ";", geta($1)); }
	| LETTER INCR
		= bundle(4, "l", $1, "d1+s", $1);
	| INCR LETTER
		= bundle(4, "l", $2, "1+ds", $2);
	| DECR LETTER
		= bundle(4, "l", $2, "1-ds", $2);
	| LETTER DECR
		= bundle(4, "l", $1, "d1-s", $1);
	| LETTER '[' e ']' INCR
		= bundle(7, $3, ";", geta($1), "d1+", $3, ":", geta($1));
	| INCR LETTER '[' e ']'
		= bundle(7, $4, ";", geta($2), "1+d", $4, ":", geta($2));
	| LETTER '[' e ']' DECR
		= bundle(7, $3, ";", geta($1), "d1-", $3, ":", geta($1));
	| DECR LETTER '[' e ']'
		= bundle(7, $4, ";", geta($2), "1-d", $4, ":", geta($2));
	| SCALE INCR
		= bundle(1, "Kd1+k");
	| INCR SCALE
		= bundle(1, "K1+dk");
	| SCALE DECR
		= bundle(1, "Kd1-k");
	| DECR SCALE
		= bundle(1, "K1-dk");
	| BASE INCR
		= bundle(1, "Id1+i");
	| INCR BASE
		= bundle(1, "I1+di");
	| BASE DECR
		= bundle(1, "Id1-i");
	| DECR BASE
		= bundle(1, "I1-di");
	| OBASE INCR
		= bundle(1, "Od1+o");
	| INCR OBASE
		= bundle(1, "O1+do");
	| OBASE DECR
		= bundle(1, "Od1-o");
	| DECR OBASE
		= bundle(1, "O1-do");
	| LETTER '(' cargs ')'
		= bundle(4, $3, "l", getf($1), "x");
	| LETTER '(' ')'
		= bundle(3, "l", getf($1), "x");
	| cons
		={ bundle(2, " ", $1); }
	| DOT cons
		={ bundle(2, " .", $2); }
	| cons DOT cons
		={ bundle(4, " ", $1, ".", $3); }
	| cons DOT
		={ bundle(3, " ", $1, "."); }
	| DOT
		={ $$ = "l."; }
	| LETTER
		={ bundle(2, "l", $1); }
	| LETTER '=' e
		={ bundle(3, $3, "ds", $1); }
	| LETTER EQOP e	%prec '='
		={ bundle(6, "l", $1, $3, $2, "ds", $1); }
	| LETTER '[' e ']' '=' e
		={ bundle(5, $6, "d", $3, ":", geta($1)); }
	| LETTER '[' e ']' EQOP e
		={ bundle(9, $3, ";", geta($1), $6, $5, "d", $3, ":", geta($1)); }
	| LENGTH '(' e ')'
		= bundle(2, $3, "Z");
	| SCALE '(' e ')'
		= bundle(2, $3, "X");	/* must be before '(' e ')' */
	| '(' e ')'
		={ $$ = $2; }
	| '?'
		={ bundle(1, "?"); }
	| SQRT '(' e ')'
		={ bundle(2, $3, "v"); }
	| '~' LETTER
		={ bundle(2, "L", $2); }
	| SCALE '=' e
		= bundle(2, $3, "dk");
	| SCALE EQOP e		%prec '='
		= bundle(4, "K", $3, $2, "dk");
	| BASE '=' e
		= bundle(2, $3, "di");
	| BASE EQOP e		%prec '='
		= bundle(4, "I", $3, $2, "di");
	| OBASE '=' e
		= bundle(2, $3, "do");
	| OBASE EQOP e		%prec '='
		= bundle(4, "O", $3, $2, "do");
	| SCALE
		= bundle(1, "K");
	| BASE
		= bundle(1, "I");
	| OBASE
		= bundle(1, "O");
	;

cargs	: eora
	| cargs ',' eora
		= bundle(2, $1, $3);
	;
eora	: e
	| LETTER '[' ']'
		= bundle(2, "l", geta($1));
	;

cons	: constant
		={ putcary('\0'); }

constant: '_'
		={ $$ = cp; putcary('_'); }
	| DIGIT
		={ $$ = cp; putcary(*$1); }
	| constant DIGIT
		={ putcary(*$2); }
	;

CRS	:
		={ $$ = crsname(); }
	;

def	: _DEFINE LETTER '('
		={ $$ = getf($2);
		   pre = "";
		   post = "";
		   lev = 1;
		   bstack[bindx = 0] = 0; }
	;

dargs	:
	| lora
		={ pp($1); }
	| dargs ',' lora
		={ pp($3); }
	;

dlets	: lora
		={ tp($1); }
	| dlets ',' lora
		={ tp($3); }
	;
lora	: LETTER
	| LETTER '[' ']'
		={ $$ = geta($1); }
	;

%%

static int peekc = -1;
static int sargc;
static int ifile;
static char **sargv;

// One dc register name per bc name, NUL-terminated in place: functions are 01..032,
// arrays 0241..0272, dc's ARRAYST and above.  A plain char is unsigned here.
static char funtab[52] = { 
	01, 0, 02, 0, 03, 0, 04, 0, 05, 0, 06, 0, 07, 0,
	010, 0, 011, 0, 012, 0, 013, 0, 014, 0, 015, 0, 016, 0,
	017, 0, 020, 0, 021, 0, 022, 0, 023, 0, 024, 0, 025, 0,
	026, 0, 027, 0, 030, 0, 031, 0, 032, 0,
};
static char atab[52] = { 
	0241, 0, 0242, 0, 0243, 0, 0244, 0, 0245, 0, 0246, 0, 0247, 0,
	0250, 0, 0251, 0, 0252, 0, 0253, 0, 0254, 0, 0255, 0, 0256, 0,
	0257, 0, 0260, 0, 0261, 0, 0262, 0, 0263, 0, 0264, 0, 0265, 0,
	0266, 0, 0267, 0, 0270, 0, 0271, 0, 0272, 0,
};
static char *letr[26] = { 
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
	"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", 
};

// A DIGIT's value: v7 put the character itself in yylval, which now holds a pointer.
static char *dig[16] = { 
	"0", "1", "2", "3", "4", "5", "6", "7",
	"8", "9", "A", "B", "C", "D", "E", "F",
};

// The bundle arena: b_space holds the elements, b_mark makes a handle a genuine
// char * rather than a cast of &b_space[i].  README.md.
static char *b_space[BSPMAX];
static char b_mark[BSPMAX];
static int b_nxt;

static _Noreturn void getout(int status);

// True when p names a bundle rather than a string.
static int isbundle(char *p)
{
	return p >= b_mark && p < &b_mark[BSPMAX];
}

// One character into the constant arena.  v7 wrote through cp with no bound.
static void putcary(int c)
{
	if (cp >= &cary[CARYSZ]) {
		yyerror("constant too long");
		getout(1);
	}
	*cp++ = c;
}

// A control structure's dc register name, and its level.  v7 left bstack[10] unchecked.
static char *crsname(void)
{
	char *s;

	if (bindx >= NEST) {
		yyerror("control structures nested too deeply");
		getout(1);
	}
	s = cp;
	putcary(crs++);
	putcary('\0');
	if (crs == '[')
		crs += 3;
	if (crs == 'a')
		crs = '{';
	if (crs >= 0241) {
		yyerror("program too big");
		getout(1);
	}
	bstack[bindx++] = lev++;
	return s;
}

// How many levels a `break' unwinds.  v7 read bstack[-1] when there was none.
static int brklev(void)
{
	if (bindx <= 0) {
		yyerror("break outside a control structure");
		return 0;
	}
	return lev - bstack[bindx - 1];
}

static char *bundle(int n, ...)
{
	va_list ap;
	int i, w, q;

	if (b_nxt + n + 1 > BSPMAX) {
		yyerror("bundling space exceeded");
		getout(1);
	}
	q = b_nxt;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		// The raw word, reinterpreted -- lib/libc/stdio/doprnt.c's own idiom.
		w = va_arg(ap, int);
		b_space[b_nxt++] = *(char **)&w;
	}
	va_end(ap);
	b_space[b_nxt++] = NULL;
	yyval = &b_mark[q];
	return yyval;
}

// Walk a bundle and print it.  Iterative: v7 recursed to a depth the input chooses.
static int rstack[RSTK];

static void routput(char *p)
{
	int n = 0;

	for (;;) {
		if (isbundle(p)) {
			if (n >= RSTK) {
				yyerror("expression nested too deeply");
				getout(1);
			}
			rstack[n++] = (int)(p - b_mark);
		} else
			fputs(p, stdout);

		for (;;) {
			if (n == 0)
				return;
			p = b_space[rstack[n - 1]++];
			if (p != NULL)
				break;
			n--;
		}
	}
}

static void output(char *p)
{
	routput(p);
	putchar('\n');
	fflush(stdout);
	b_nxt = 0;
	cp = cary;
	sp = sary;
	crs = rcrs;
}

static void conout(char *p, char *s)
{
	putchar('[');
	routput(p);
	printf("]s%s\n", s);
	fflush(stdout);
	lev--;
}

void yyerror(char *s)
{
	if (ifile > sargc)
		ss = "teletype";
	printf("c[%s on line %d, %s]pc\n", s, ln + 1, ss);
	fflush(stdout);
	b_nxt = 0;
	cp = cary;
	sp = sary;
	crs = rcrs;
	bindx = 0;
	lev = 0;
}

// Put the relevant stuff on pre and post for the letter s.
static void pp(char *s)
{
	pre = bundle(3, "S", s, pre);
	post = bundle(4, post, "L", s, "s.");
}

// The same as pp(), but for temporaries.
static void tp(char *s)
{
	pre = bundle(3, "0S", s, pre);
	post = bundle(4, post, "L", s, "s.");
}

static _Noreturn void getout(int status)
{
	printf("q");
	fflush(stdout);
	exit(status);
}

static char *getf(char *p)
{
	return &funtab[2 * (*p - 0141)];
}

static char *geta(char *p)
{
	return &atab[2 * (*p - 0141)];
}

static int getch(void)
{
	int ch;

loop:
	ch = (peekc < 0) ? getc(inp) : peekc;
	peekc = -1;
	if (ch != EOF)
		return ch;
	if (++ifile > sargc) {
		if (ifile >= sargc + 2)
			getout(0);
		inp = stdin;
		ln = 0;
		goto loop;
	}
	fclose(inp);
	if ((inp = fopen(sargv[ifile], "r")) != NULL) {
		ln = 0;
		ss = sargv[ifile];
		goto loop;
	}
	yyerror("cannot open input file");
	getout(1);
}

static int cpeek(int c, int yes, int no)
{
	if ((peekc = getch()) != c)
		return no;
	peekc = -1;
	return yes;
}

int yylex(void)
{
	int c, ch;

restart:
	c = getch();
	peekc = -1;
	while (c == ' ' || c == '\t')
		c = getch();
	if (c == '\\') {
		getch();
		goto restart;
	}
	if (c <= 'z' && c >= 'a') {
		// Look ahead for a reserved word: two letters name it, and any letter
		// after them is part of it.
		peekc = getch();
		if (peekc >= 'a' && peekc <= 'z') {
			if (c == 'i' && peekc == 'f')
				c = _IF;
			else if (c == 'w' && peekc == 'h')
				c = _WHILE;
			else if (c == 'f' && peekc == 'o')
				c = _FOR;
			else if (c == 's' && peekc == 'q')
				c = SQRT;
			else if (c == 'r' && peekc == 'e')
				c = _RETURN;
			else if (c == 'b' && peekc == 'r')
				c = _BREAK;
			else if (c == 'd' && peekc == 'e')
				c = _DEFINE;
			else if (c == 's' && peekc == 'c')
				c = SCALE;
			else if (c == 'b' && peekc == 'a')
				c = BASE;
			else if (c == 'i' && peekc == 'b')
				c = BASE;
			else if (c == 'o' && peekc == 'b')
				c = OBASE;
			else if (c == 'd' && peekc == 'i')
				c = FFF;
			else if (c == 'a' && peekc == 'u')
				c = _AUTO;
			else if (c == 'l' && peekc == 'e')
				c = LENGTH;
			else if (c == 'q' && peekc == 'u')
				getout(0);
			else
				return YYERRCODE;
			peekc = -1;
			while ((ch = getch()) >= 'a' && ch <= 'z')
				;
			peekc = ch;
			return c;
		}

		// The usual case: just one single letter.
		yylval = letr[c - 'a'];
		return LETTER;
	}
	if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
		yylval = dig[c <= '9' ? c - '0' : c - 'A' + 10];
		return DIGIT;
	}
	switch (c) {
	case '.':
		return DOT;
	case '=':
		switch (peekc = getch()) {
		case '=':
			c = EQ;
			break;
		case '+':
			c = EQPL;
			break;
		case '-':
			c = EQMI;
			break;
		case '*':
			c = EQMUL;
			break;
		case '/':
			c = EQDIV;
			break;
		case '%':
			c = EQREM;
			break;
		case '^':
			c = EQEXP;
			break;
		default:
			return '=';
		}
		peekc = -1;
		return c;
	case '+':
		return cpeek('+', INCR, '+');
	case '-':
		return cpeek('-', DECR, '-');
	case '<':
		return cpeek('=', LE, '<');
	case '>':
		return cpeek('=', GE, '>');
	case '!':
		return cpeek('=', NE, '!');
	case '/':
		if ((peekc = getch()) != '*')
			return c;
		peekc = -1;
		while (getch() != '*' || (peekc = getch()) != '/')
			;
		peekc = -1;
		goto restart;
	case '"':
		yylval = sp;
		while ((c = getch()) != '"') {
			if (sp >= &sary[STRSZ - 1]) {
				yyerror("string space exceeded");
				getout(1);
			}
			*sp++ = c;
		}
		*sp++ = '\0';
		return QSTR;
	default:
		return c;
	}
}

static void yyinit(int argc, char **argv)
{
	signal(SIGINT, SIG_IGN);	// ignore all interrupts
	sargv = argv;
	sargc = --argc;
	ifile = 1;
	ln = 0;
	// Named before the open, so the diagnostic can say which file, and fatal: v7
	// diagnosed with ss still unset and then read through the null FILE *.
	ss = sargc > 0 ? sargv[1] : "teletype";
	if (sargc == 0)
		inp = stdin;
	else if ((inp = fopen(sargv[1], "r")) == NULL) {
		yyerror("cannot open input file");
		getout(1);
	}
}

// The file list yyinit() walks, argv-shaped.  v7 parsed one flag and overwrote argv[1].
static char *fv[NFILES + 2];

int main(int argc, char **argv)
{
	int p[2];
	int cflag = 0, lflag = 0, fc = 1;

	while (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0') {
		switch (argv[1][1]) {
		case 'c':
		case 'd':
			cflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		default:
			fprintf(stderr, "bc: unrecognizable argument %s\n", argv[1]);
			exit(1);
		}
		argc--;
		argv++;
	}
	fv[0] = "bc";
	if (lflag)
		fv[fc++] = LIBB;
	while (argc > 1) {
		if (fc > NFILES) {
			fprintf(stderr, "bc: too many files\n");
			exit(1);
		}
		fv[fc++] = argv[1];
		argc--;
		argv++;
	}
	fv[fc] = NULL;

	if (cflag) {
		yyinit(fc, fv);
		yyparse();
		exit(0);
	}
	if (pipe(p) < 0) {
		perror("bc: pipe");
		exit(1);
	}
	switch (fork()) {
	case -1:
		perror("bc: fork");
		exit(1);
	case 0:
		close(1);
		dup(p[1]);
		close(p[0]);
		close(p[1]);
		yyinit(fc, fv);
		yyparse();
		exit(0);
	}
	close(0);
	dup(p[0]);
	close(p[0]);
	close(p[1]);
	execl("/bin/dc", "dc", "-", (char *)NULL);
	execl("/usr/bin/dc", "dc", "-", (char *)NULL);
	perror("bc: /bin/dc");
	exit(1);
}
