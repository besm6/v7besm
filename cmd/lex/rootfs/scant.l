%{
//
// scant -- task C10b's native assertion, and NOT a program on the image.  It is
// the only thing that says b6parse accepts the C11 ncform and that a generated
// scanner scans on a BESM-6; it is also b6_lex()'s first caller.
// ../README.md, "The native case".
//
// Four things the port rewrote, one per case: the yylook() walk, a state packed
// by character (the signed yystoff offset), start conditions, and eight bits.
//
#include <stdio.h>

int nkey, nid, nnum, nhigh;
%}

%Start CMT

	/* every rule is qualified: an UNQUALIFIED lex rule is active in every
	   start condition, so a bare "if" would match inside a comment too. */
%%
<INITIAL>"if"		nkey++;
<INITIAL>"while"	nkey++;
<INITIAL>"/*"		BEGIN CMT;
<CMT>"*/"		BEGIN INITIAL;
<CMT>.			;
<CMT>\n			;
<INITIAL>[a-z][a-z0-9]*	nid++;
<INITIAL>[0-9]+		nnum++;
<INITIAL>[\200-\377]+	{ nhigh += yyleng; printf("high %d %d\n", yyleng, (unsigned char)yytext[0]); }
<INITIAL>[ \t\n]		;
<INITIAL>.		printf("odd %d\n", (unsigned char)yytext[0]);
%%

int yywrap(void)
{
	return 1;
}

int main(void)
{
	yylex();
	printf("keywords %d ids %d numbers %d highbytes %d\n", nkey, nid, nnum, nhigh);
	return 0;
}
