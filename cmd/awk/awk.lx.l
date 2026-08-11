%Start A str chc sc reg comment

%{
#include	"awk.h"
#include	"y.tab.h"
#include	<stdio.h>
#include	<stdlib.h>

static int fieldno(char *s)	/* atoi() is 93 words of libc for this */
{
	int n = 0;
	while (*s >= '0' && *s <= '9')
		n = n * 10 + *s++ - '0';
	return n;
}
#undef	input	/* defeat lex */
extern YYSTYPE	yylval;

int	lineno	= 1;
#define	RETURN(x)	return(x)
#define	CBUFLEN	150
#define	CADD	cbuf[clen++]=yytext[0]; if(clen>=CBUFLEN-1) {yyerror("string too long"); BEGIN A;}
static char	cbuf[CBUFLEN];
static int	clen, cflag;
%}

A	[a-zA-Z_]
B	[a-zA-Z0-9_]
D	[0-9]
WS	[ \t]

%%
	switch (yybgin-yysvec-1) {	/* witchcraft */
	case 0:
		BEGIN A;
		break;
	case sc:
		BEGIN A;
		RETURN('}');
	}

<A>^\n		lineno++;
<A>^{WS}*#.*\n	lineno++;	/* strip comment lines */
<A>{WS}		;
<A,reg>"\\"\n	lineno++;
<A>"||"		RETURN(BOR);
<A>BEGIN	RETURN(XBEGIN);
<A>END		RETURN(XEND);
<A>PROGEND	RETURN(EOF);
<A>"&&"		RETURN(AND);
<A>"!"		RETURN(NOT);
<A>"!="		{ yylval = (node *)NE; RETURN(RELOP); }
<A>"~"		{ yylval = (node *)MATCH; RETURN(MATCHOP); }
<A>"!~"		{ yylval = (node *)NOTMATCH; RETURN(MATCHOP); }
<A>"<"		{ yylval = (node *)LT; RETURN(RELOP); }
<A>"<="		{ yylval = (node *)LE; RETURN(RELOP); }
<A>"=="		{ yylval = (node *)EQ; RETURN(RELOP); }
<A>">="		{ yylval = (node *)GE; RETURN(RELOP); }
<A>">"		{ yylval = (node *)GT; RETURN(RELOP); }
<A>">>"		{ yylval = (node *)APPEND; RETURN(RELOP); }
<A>"++"		{ yylval = (node *)INCR; RETURN(INCR); }
<A>"--"		{ yylval = (node *)DECR; RETURN(DECR); }
<A>"+="		{ yylval = (node *)ADDEQ; RETURN(ASGNOP); }
<A>"-="		{ yylval = (node *)SUBEQ; RETURN(ASGNOP); }
<A>"*="		{ yylval = (node *)MULTEQ; RETURN(ASGNOP); }
<A>"/="		{ yylval = (node *)DIVEQ; RETURN(ASGNOP); }
<A>"%="		{ yylval = (node *)MODEQ; RETURN(ASGNOP); }
<A>"="		{ yylval = (node *)ASSIGN; RETURN(ASGNOP); }

<A>"$"{D}+	{	if (fieldno(yytext+1)==0) {
				yylval = (node *)lookup("$record", symtab, 0);
				RETURN(STRING);
			} else {
				yylval = (node *)fieldadr(fieldno(yytext+1));
				RETURN(FIELD);
			}
		}
<A>"$"{WS}*	{ RETURN(INDIRECT); }
<A>NF		{ mustfld=1; yylval = (node *)setsymtab(yytext, NULL, 0.0, NUM, symtab); RETURN(VAR); }
<A>({D}+("."?){D}*|"."{D}+)((e|E)("+"|-)?{D}+)?	{
		double v = 0.0;
		if (isnumstr(yytext)) v = atof(yytext);
		else yyerror("number out of range");
		yylval = (node *)setsymtab(yytext, NULL, v, CON|NUM, symtab); RETURN(NUMBER); }
<A>"}"{WS}*\n	{ BEGIN sc; lineno++; RETURN(';'); }
<A>"}"		{ BEGIN sc; RETURN(';'); }
<A>;\n		{ lineno++; RETURN(';'); }
<A>\n		{ lineno++; RETURN(NL); }
<A>while	RETURN(WHILE);
<A>for		RETURN(FOR);
<A>if		RETURN(IF);
<A>else		RETURN(ELSE);
<A>next		RETURN(NEXT);
<A>exit		RETURN(EXIT);
<A>break	RETURN(BREAK);
<A>continue	RETURN(CONTINUE);
<A>print	{ yylval = (node *)PRINT; RETURN(PRINT); }
<A>printf	{ yylval = (node *)PRINTF; RETURN(PRINTF); }
<A>sprintf	{ yylval = (node *)SPRINTF; RETURN(SPRINTF); }
<A>split	{ yylval = (node *)SPLIT; RETURN(SPLIT); }
<A>substr	RETURN(SUBSTR);
<A>index	RETURN(INDEX);
<A>in		RETURN(IN);
<A>getline	RETURN(GETLINE);
<A>length	{ yylval = (node *)FLENGTH; RETURN(FNCN); }
<A>log		{ yylval = (node *)FLOG; RETURN(FNCN); }
<A>int		{ yylval = (node *)FINT; RETURN(FNCN); }
<A>exp		{ yylval = (node *)FEXP; RETURN(FNCN); }
<A>sqrt		{ yylval = (node *)FSQRT; RETURN(FNCN); }
<A>{A}{B}*	{ yylval = (node *)setsymtab(yytext, tostring(""), 0.0, STR|NUM, symtab); RETURN(VAR); }
<A>\"		{ BEGIN str; clen=0; }

<A>#		{ BEGIN comment; }
<comment>\n	{ BEGIN A; lineno++; RETURN(NL); }
<comment>.	;

<A>.		{ yylval = (node *)yytext[0]; RETURN(yytext[0]); }

<reg>"["	{ BEGIN chc; clen=0; cflag=0; }
<reg>"[^"	{ BEGIN chc; clen=0; cflag=1; }

<reg>"?"	RETURN(QUEST);
<reg>"+"	RETURN(PLUS);
<reg>"*"	RETURN(STAR);
<reg>"|"	RETURN(OR);
<reg>"."	RETURN(DOT);
<reg>"("	RETURN('(');
<reg>")"	RETURN(')');
<reg>"^"	RETURN('^');
<reg>"$"	RETURN('$');
<reg>\\{D}{D}{D}	{ yylval = (node *)((yytext[1]-'0')*0100 + (yytext[2]-'0')*010 + (yytext[3]-'0')); RETURN(CHAR); }
<reg>\\.	{	if (yytext[1]=='n') yylval = (node *)'\n';
			else if (yytext[1] == 't') yylval = (node *)'\t';
			else yylval = (node *)yytext[1];
			RETURN(CHAR);
		}
<reg>"/"	{ BEGIN A; unput('/'); }
<reg>\n		{ yyerror("newline in regular expression"); lineno++; BEGIN A; }
<reg>.		{ yylval = (node *)yytext[0]; RETURN(CHAR); }

<str>\"		{ char *s; BEGIN A; cbuf[clen]=0; s = tostring(cbuf);
		cbuf[clen] = ' '; cbuf[++clen] = 0;
		yylval = (node *)setsymtab(cbuf, s, 0.0, CON|STR, symtab); RETURN(STRING); }
<str>\n		{ yyerror("newline in string"); lineno++; BEGIN A; }
<str>"\\\""	{ cbuf[clen++]='"'; }
<str,chc>"\\"n	{ cbuf[clen++]='\n'; }
<str,chc>"\\"t	{ cbuf[clen++]='\t'; }
<str,chc>"\\\\"	{ cbuf[clen++]='\\'; }
<str>.		{ CADD; }

<chc>"\\""]"	{ cbuf[clen++]=']'; }
<chc>"]"	{ BEGIN reg; cbuf[clen]=0; yylval = (node *)cclstash(tostring(cbuf));
		if (cflag==0) { RETURN(CCL); }
		else { RETURN(NCCL); } }
<chc>\n		{ yyerror("newline in character class"); lineno++; BEGIN A; }
<chc>.		{ CADD; }

%%

int input(void)
{
	register int c;
	extern char *lexprog;

	if (yysptr > yysbuf)
		c = U(*--yysptr);
	else if (yyin == NULL)
		c = *lexprog++;
	else
		c = getc(yyin);
	if (c == '\n')
		yylineno++;
	else if (c == EOF)
		c = 0;
	return(c);
}

void startreg(void)
{
	BEGIN reg;
}
