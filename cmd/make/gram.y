%{
#include "defs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int retsh(char *q);
static int nextlin(void);
%}

%term NAME SHELLINE START MACRODEF COLON DOUBLECOLON GREATER
%union {
	struct shblock *yshblock;
	struct depblock *ydepblock;
	struct nameblock *ynameblock;
}

%type <yshblock> SHELLINE, shlist, shellist
%type <ynameblock> NAME, namelist
%type <ydepblock> deplist, dlist

%%

%{
struct depblock *pp;
static struct shblock *prevshp;

static struct nameblock *lefts[NLEFTS];
struct nameblock *leftp;
static int nlefts;

struct lineblock *lp, *lpp;
static struct depblock *prevdep;
static int sepc;
%}

file	:
	| file comline
	;

comline	: START
	| MACRODEF
	| START namelist deplist shellist ={
		while (--nlefts >= 0) {
		    leftp = lefts[nlefts];
		    if (leftp->septype == 0)
			leftp->septype = sepc;
		    else if (leftp->septype != sepc)
			fprintf(stderr, "Inconsistent rules lines for `%s'\n",
				leftp->namep);
		    else if (sepc == ALLDEPS && *(leftp->namep) != '.' && $4 != 0) {
			for (lp = leftp->linep; lp->nxtlineblock != 0;
			     lp = lp->nxtlineblock)
			    if (lp->shp)
				fprintf(stderr, "Multiple rules lines for `%s'\n",
					leftp->namep);
		    }

		    lp = ALLOC(lineblock);
		    lp->nxtlineblock = NULL;
		    lp->depp = $3;
		    lp->shp = $4;

		    if (!unequal(leftp->namep, ".SUFFIXES") && $3 == 0)
			leftp->linep = 0;
		    else if (leftp->linep == 0)
			leftp->linep = lp;
		    else {
			for (lpp = leftp->linep; lpp->nxtlineblock;
			     lpp = lpp->nxtlineblock)
			    ;
			if (sepc == ALLDEPS && leftp->namep[0] == '.')
			    lpp->shp = 0;
			lpp->nxtlineblock = lp;
		    }
		}
	}
	| error
	;

namelist: NAME		={ lefts[0] = $1; nlefts = 1; }
	| namelist NAME	={
		if (nlefts >= NLEFTS)
		    fatal("Too many lefts");
		lefts[nlefts++] = $2;
	}
	;

deplist	: {
		char junk[10];

		sprintf(junk, "%d", yylineno);
		fatal1("Must be a separator on rules line %s", junk);
	}
	| dlist
	;

dlist	: sepchar	={ prevdep = 0; $$ = 0; }
	| dlist NAME	={
		pp = ALLOC(depblock);
		pp->nxtdepblock = NULL;
		pp->depname = $2;
		if (prevdep == 0)
		    $$ = pp;
		else
		    prevdep->nxtdepblock = pp;
		prevdep = pp;
	}
	;

sepchar	: COLON		={ sepc = ALLDEPS; }
	| DOUBLECOLON	={ sepc = SOMEDEPS; }
	;

shellist:		={ $$ = 0; }
	| shlist	={ $$ = $1; }
	;

shlist	: SHELLINE	={ $$ = $1; prevshp = $1; }
	| shlist SHELLINE ={
		$$ = $1;
		prevshp->nxtshblock = $2;
		prevshp = $2;
	}
	;

%%

char *zznextc; // zero if need another line; otherwise points to next char
int yylineno;

// Scratch at file scope, not on the stack: three INMAX char arrays are 750 words
// and yylex() sits under yyparse() for the whole of a description file.
static char word[INMAX];
static char templin[INMAX];
static char yytext[INMAX];

int yylex(void)
{
    char *p;
    char *q;

    if (zznextc == 0)
        return nextlin();

    while (isspace((unsigned char)*zznextc))
        ++zznextc;

    if (*zznextc == '\0')
        return nextlin();

    if (*zznextc == ':') {
        if (*++zznextc == ':') {
            ++zznextc;
            return DOUBLECOLON;
        } else
            return COLON;
    }

    if (*zznextc == '>') {
        ++zznextc;
        return GREATER;
    }

    if (*zznextc == ';')
        return retsh(zznextc);

    p = zznextc;
    q = word;

    while (!(funny[(unsigned char)*p] & TERMINAL)) {
        if (q >= &word[INMAX - 1])
            fatal("word too long");
        *q++ = *p++;
    }

    if (p != zznextc) {
        *q = '\0';
        if ((yylval.ynameblock = srchname(word)) == 0)
            yylval.ynameblock = makename(word);
        zznextc = p;
        return NAME;
    }

    else {
        fprintf(stderr, "Bad character %c (octal %o), line %d", *zznextc, *zznextc,
                yylineno);
        fatal((char *)NULL);
    }
    return 0; // never executed
}

static int retsh(char *q)
{
    char *p;
    struct shblock *sp;

    for (p = q + 1; *p == ' ' || *p == '\t'; ++p)
        ;

    sp             = ALLOC(shblock);
    sp->nxtshblock = NULL;
    sp->shbp       = copys(p); // v7 kept the pointer when reading builtin[]
    yylval.yshblock = sp;
    zznextc        = 0;
    return SHELLINE;
}

static int nextlin(void)
{
    static char *yytextl = yytext + INMAX;
    char *text;
    char c;
    char *p, *t;
    char lastch, *lastchp;
    int incom;
    int kc;

again:

    incom   = NO;
    zznextc = 0;

    if (fin == NULL) {
        // A builtin line is copied rather than parsed in place: eqsign() and the
        // substitution below write into it, and a literal is not writable here.
        if ((t = *linesptr++) == 0)
            return 0;
        ++yylineno;
        for (p = yytext; p < yytextl && (*p = *t++) != '\0'; ++p)
            ;
        if (p >= yytextl)
            fatal("line too long");
        text = yytext;
    }

    else {
        text = yytext;
        for (p = yytext; p < yytextl; *p++ = kc) switch (kc = getc(fin)) {
            case '\t':
                if (p != yytext)
                    break;
                // fall through: a tab in column 1 opens a command line
            case ';':
                incom = YES;
                break;

            case '#':
                if (!incom)
                    kc = '\0';
                break;

            case '\n':
                ++yylineno;
                if (p == yytext || p[-1] != '\\') {
                    *p = '\0';
                    goto endloop;
                }
                p[-1] = ' ';
                while ((kc = getc(fin)) == '\t' || kc == ' ' || kc == '\n')
                    if (kc == '\n')
                        ++yylineno;

                if (kc != EOF)
                    break;
                // fall through
            case EOF:
                *p = '\0';
                return 0;
            }

        fatal("line too long");
    }

endloop:

    if ((c = text[0]) == '\t')
        return retsh(text);

    if (isalpha((unsigned char)c) || isdigit((unsigned char)c) || c == ' ' || c == '.')
        for (p = text + 1; *p != '\0';)
            if (*p == ':')
                break;
            else if (*p++ == '=') {
                eqsign(text);
                return MACRODEF;
            }

    // substitute for macros on dependency line up to the semicolon if any
    for (t = text; *t != '\0' && *t != ';'; ++t)
        ;

    lastchp = t;
    lastch  = *t;
    *t      = '\0';

    subst(text, templin, templin + INMAX - 1);

    if (lastch) {
        for (t = templin; *t; ++t)
            ;
        if (t >= &templin[INMAX - 1])
            fatal("macro expansion too long");
        *t = lastch;
        while (t < &templin[INMAX - 1] && (*++t = *++lastchp))
            ;
        *t = '\0';
    }

    p = templin;
    t = text;
    while ((*t++ = *p++))
        ;

    for (p = zznextc = text; *p; ++p)
        if (*p != ' ' && *p != '\t')
            return START;
    goto again;
}
