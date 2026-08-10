//
// Parser for yacc output.  DATA to yacc and SOURCE to its caller: others() in
// y1.c copies it into y.tab.c verbatim, so it is compiled by b6cc or by the host
// compiler and has to be C11.  It was not.  README.md, "The skeleton".
//
// The action marker at the bottom may appear exactly once, comments included.
// Prototypes for yylex() and yyerror() cannot live here; y2.c emits them.
//

#define YYFLAG   -1000
#define YYERROR  goto yyerrlab
#define YYACCEPT return (0)
#define YYABORT  return (1)

#ifdef YYDEBUG
int yydebug = 0; // 1 for debugging
#endif

// One extra slot on each stack: index 0 is a sentinel.  v7 started both at
// &array[-1], a pointer below its own object (../README.md SS2).  The usable
// depth is still YYMAXDEPTH.
YYSTYPE yyv[YYMAXDEPTH + 1]; // where the values are stored
int yychar      = -1;        // current input token number
int yynerrs     = 0;         // number of errors
short yyerrflag = 0;         // error recovery flag

int yyparse(void)
{
    short yys[YYMAXDEPTH + 1];
    short yyj, yym;
    YYSTYPE *yypvt;
    short yystate, *yyps, yyn;
    YYSTYPE *yypv;
    short *yyxi;

    yystate   = 0;
    yychar    = -1;
    yynerrs   = 0;
    yyerrflag = 0;
    yyps      = yys; // the sentinel, not &yys[-1]
    yypv      = yyv;

yystack: // put a state and value onto the stack

#ifdef YYDEBUG
    if (yydebug)
        printf("state %d, char 0%o\n", yystate, yychar);
#endif
    if (++yyps > &yys[YYMAXDEPTH]) {
        yyerror("yacc stack overflow");
        return (1);
    }
    *yyps = yystate;
    ++yypv;
    *yypv = yyval;

yynewstate:

    yyn = yypact[yystate];

    if (yyn <= YYFLAG)
        goto yydefault; // simple state

    if (yychar < 0)
        if ((yychar = yylex()) < 0)
            yychar = 0;
    if ((yyn += yychar) < 0 || yyn >= YYLAST)
        goto yydefault;

    if (yychk[yyn = yyact[yyn]] == yychar) { // valid shift
        yychar  = -1;
        yyval   = yylval;
        yystate = yyn;
        if (yyerrflag > 0)
            --yyerrflag;
        goto yystack;
    }

yydefault:
    // default state action

    if ((yyn = yydef[yystate]) == -2) {
        if (yychar < 0)
            if ((yychar = yylex()) < 0)
                yychar = 0;
        // look through exception table

        for (yyxi = yyexca; (*yyxi != (-1)) || (yyxi[1] != yystate); yyxi += 2)
            ; // VOID

        while (*(yyxi += 2) >= 0) {
            if (*yyxi == yychar)
                break;
        }
        if ((yyn = yyxi[1]) < 0)
            return (0); // accept
    }

    if (yyn == 0) { // error
        // error ... attempt to resume parsing

        switch (yyerrflag) {
        case 0: // brand new error

            yyerror("syntax error");
        yyerrlab:
            ++yynerrs;
            // FALLTHROUGH

        case 1:
        case 2: // incompletely recovered error ... try again

            yyerrflag = 3;

            // find a state where "error" is a legal shift action

            while (yyps > yys) { // yys[0] is the sentinel
                yyn = yypact[*yyps] + YYERRCODE;
                if (yyn >= 0 && yyn < YYLAST && yychk[yyact[yyn]] == YYERRCODE) {
                    yystate = yyact[yyn]; // simulate a shift of "error"
                    goto yystack;
                }
                yyn = yypact[*yyps];

                // the current yyps has no shift on "error", pop stack

#ifdef YYDEBUG
                if (yydebug)
                    printf("error recovery pops state %d, uncovers %d\n", *yyps, yyps[-1]);
#endif
                --yyps;
                --yypv;
            }

            // there is no state on the stack with an error shift ... abort

        yyabort:
            return (1);

        case 3: // no shift yet; clobber input char

#ifdef YYDEBUG
            if (yydebug)
                printf("error recovery discards char %d\n", yychar);
#endif

            if (yychar == 0)
                goto yyabort; // don't discard EOF, quit
            yychar = -1;
            goto yynewstate; // try again in the same state
        }
    }

    // reduction by production yyn

#ifdef YYDEBUG
    if (yydebug)
        printf("reduce %d\n", yyn);
#endif
    yyps -= yyr2[yyn];
    yypvt = yypv;
    yypv -= yyr2[yyn];
    yyval = yypv[1];
    yym   = yyn;
    // consult goto table to find next state
    yyn = yyr1[yyn];
    yyj = yypgo[yyn] + *yyps + 1;
    if (yyj >= YYLAST || yychk[yystate = yyact[yyj]] != -yyn)
        yystate = yyact[yypgo[yyn]];
    switch (yym) {
        $A
    }
    goto yystack; // stack new state and value
}
