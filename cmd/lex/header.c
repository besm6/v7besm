#include "ldefs.h"

//
// The generated scanner's header block.  It carries the PROTOTYPES ncform
// needs: the skeleton is appended last, after the user's own subroutines, but
// yylex() -- emitted in the middle by phead2() -- already calls yylook() and
// yywrap().  cmd/yacc does the same for yaccpar.c's sake, and for the same
// reason (../yacc/README.md, "The contract").
//
void phead1(void)
{
    fprintf(fout, "#include <stdio.h>\n");
    // Always mask.  A byte above 127 is a value in 128..255, and on a host
    // where char is signed it indexes yymatch[] from below zero otherwise.
    fprintf(fout, "# define U(x) ((x)&0377)\n");
    fprintf(fout, "# define NLSTATE yyprevious=YYNEWLINE\n");
    fprintf(fout, "# define BEGIN yybgin = yysvec + 1 +\n");
    fprintf(fout, "# define INITIAL 0\n");
    fprintf(fout, "# define YYLERR yysvec\n");
    fprintf(fout, "# define YYSTATE (yyestate-yysvec-1)\n");
    if (optim)
        fprintf(fout, "# define YYOPTIM 1\n");
    fprintf(fout, "# define YYLMAX 200\n");
    // yyin and yyout cannot be initialised where they are defined -- stdin is
    // not a constant expression -- so they resolve on first use.
    fprintf(fout, "# define YYIN (yyin != NULL ? yyin : (yyin = stdin))\n");
    fprintf(fout, "# define YYOUT (yyout != NULL ? yyout : (yyout = stdout))\n");
    fprintf(fout, "# define output(c) putc(c,YYOUT)\n");
    fprintf(fout, "%s%d%s\n",
            "# define input() (((yytchar=yysptr>yysbuf?U(*--yysptr):getc(YYIN))==", ctable['\n'],
            "?(yylineno++,yytchar):yytchar)==EOF?0:yytchar)");
    fprintf(fout, "# define unput(c) {yytchar= (c);if(yytchar=='\\n')yylineno--;*yysptr++=yytchar;}\n");
    fprintf(fout, "# define yymore() (yymorfg=1)\n");
    fprintf(fout, "# define ECHO fprintf(YYOUT, \"%%s\",yytext)\n");
    fprintf(fout, "# define REJECT { nstr = yyreject(); goto yyfussy;}\n");
    fprintf(fout, "int yyleng; extern char yytext[];\n");
    fprintf(fout, "int yymorfg;\n");
    fprintf(fout, "extern char *yysptr, yysbuf[];\n");
    fprintf(fout, "int yytchar;\n");
    fprintf(fout, "FILE *yyin, *yyout;\n");
    fprintf(fout, "extern int yylineno;\n");
    // yystoff is a SIGNED OFFSET into yycrank[], negative for a char-compressed
    // state.  v7 made it a struct yywork * and emitted pointers below the base
    // of that array.  README.md, "The skeleton".
    fprintf(fout, "struct yysvf { \n");
    fprintf(fout, "\tint yystoff;\n");
    fprintf(fout, "\tstruct yysvf *yyother;\n");
    fprintf(fout, "\tint *yystops;};\n");
    fprintf(fout, "struct yysvf *yyestate;\n");
    fprintf(fout, "extern struct yysvf yysvec[], *yybgin;\n");
    // The contract.  yywrap() is the scanner's -- there is no libl.a on this
    // system -- and the rest are the skeleton's.
    fprintf(fout, "int yylook(void);\n");
    fprintf(fout, "int yyback(int *, int);\n");
    fprintf(fout, "int yywrap(void);\n");
    fprintf(fout, "int yyreject(void);\n");
    fprintf(fout, "int yyracc(int);\n");
    fprintf(fout, "void yyless(int);\n");
    fprintf(fout, "int yyinput(void);\n");
    fprintf(fout, "void yyoutput(int);\n");
    fprintf(fout, "void yyunput(int);\n");
}

void phead2(void)
{
    fprintf(fout, "while((nstr = yylook()) >= 0)\n");
    fprintf(fout, "yyfussy: switch(nstr){\n");
    fprintf(fout, "case 0:\n");
    fprintf(fout, "if(yywrap()) return(0); break;\n");
}

void ptail(void)
{
    if (pflag)
        return;
    pflag = 1;
    fprintf(fout, "case -1:\nbreak;\n"); // for reject
    fprintf(fout, "default:\n");
    fprintf(fout, "fprintf(YYOUT,\"bad switch yylook %%d\",nstr);\n");
    fprintf(fout, "} return(0); }\n");
    fprintf(fout, "/* end of yylex */\n");
}

// One line, and every ratio names the %-option that raises its bound.
void statistics(void)
{
    fprintf(errorf, "%d/%d nodes(%%e), %d/%d positions(%%p), %d/%d (%%n), %d transitions",
            tptr, treesize, (int)(nxtpos - positions), maxpos, stnum + 1, nstates, rcount);
    fprintf(errorf, ", %d/%d packed char classes(%%k)", (int)(pcptr - pchar), pchlen);
    if (optim)
        fprintf(errorf, ", %d/%d packed transitions(%%a)", nptr, ntrans);
    fprintf(errorf, ", %d/%d output slots(%%o)", yytop, outsize);
    putc('\n', errorf);
}
