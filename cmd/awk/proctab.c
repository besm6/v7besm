// The dispatch table execute() indexes by token, filled at startup.  v7 generated this
// file by building and running proc.c; README.md says why that is gone.
//
// It cannot be a sparse static initializer: b6lower ignores designated initializers.
#include "awk.h"
#include "y.tab.h"

#define NPROC (LASTTOKEN - FIRSTTOKEN)

static const struct {
    int token;
    procfn fn;
} procs[] = {
    { PROGRAM, program },    { BOR, boolop },       { AND, boolop },        { NOT, boolop },
    { NE, relop },           { EQ, relop },         { LE, relop },          { LT, relop },
    { GE, relop },           { GT, relop },         { ARRAY, array },       { INDIRECT, indirect },
    { SUBSTR, substr },      { INDEX, sindex },     { SPRINTF, asprintf },  { ADD, arith },
    { MINUS, arith },        { MULT, arith },       { DIVIDE, arith },      { MOD, arith },
    { UMINUS, arith },       { PREINCR, incrdecr }, { POSTINCR, incrdecr }, { PREDECR, incrdecr },
    { POSTDECR, incrdecr },  { CAT, cat },          { PASTAT, pastat },     { PASTAT2, dopa2 },
    { MATCH, matchop },      { NOTMATCH, matchop }, { PRINTF, aprintf },    { PRINT, print },
    { SPLIT, split },        { ASSIGN, assign },    { ADDEQ, assign },      { SUBEQ, assign },
    { MULTEQ, assign },      { DIVEQ, assign },     { MODEQ, assign },      { IF, ifstat },
    { WHILE, whilestat },    { FOR, forstat },      { IN, instat },         { NEXT, jump },
    { EXIT, jump },          { BREAK, jump },       { CONTINUE, jump },     { FNCN, fncn },
    { GETLINE, awkgetline },
};

procfn proctab[NPROC];

void procinit(void)
{
    int i;

    for (i = 0; i < NPROC; i++)
        proctab[i] = nullproc;
    for (i = 0; i < (int)(sizeof procs / sizeof procs[0]); i++)
        proctab[procs[i].token - FIRSTTOKEN] = procs[i].fn;
}
