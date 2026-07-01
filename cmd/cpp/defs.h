#include <stdio.h>

enum {
    YYEOF,
    number,
    stop,
    DEFINED,
    EQ,
    NE,
    LE,
    GE,
    LS,
    RS,
    ANDAND,
    OROR,
    UMINUS,
};

struct symtab {
    char *name;
    char *value;
};

#define ALFSIZ 256 // alphabet size
#ifndef BUFSIZ
#define BUFSIZ 512
#endif
#define SBSIZE  12000
#define MAXINC  10
#define MAXFRE  14 // max buffers of macro pushback
#define NPREDEF 20
#define symsiz  400

//
// All mutable per-run state of the preprocessor, bundled into one instance
// (see cpp.c) so it is namespaced instead of scattered across file-scope
// globals.  Shared by cpp.c, parser.c and yylex.c.
//
struct cppstate {
    // scan buffer and cursors
    char *pbeg, *pbuf, *pend; // buffer bounds
    char *outp, *inp, *newp;  // output / input / macro-expansion cursors
    char cinit;               // hash seed
    char buffer[8 + BUFSIZ + BUFSIZ + 8];

    // character-class / scan tables (built at startup)
    char macbit[ALFSIZ + 11];
    char toktyp[ALFSIZ];
    char fastab[ALFSIZ];
    char slotab[ALFSIZ];
    char *ptrtab; // active scan table

    // side buffer for macro pushback
    char sbf[SBSIZE];
    char *savch; // init: sbf
    int mactop, fretop;
    char *instack[MAXFRE], *bufstack[MAXFRE], *endbuf[MAXFRE];

    // in-flight macro call context
    int plvl, maclin, maclvl, macdam;
    char *macfil, *macnam, *macforw;

    // include stack
    int inctop[MAXINC], fins[MAXINC], lineno[MAXINC];
    char *fnames[MAXINC], *dirnams[MAXINC];
    char *dirs[10];
    int fin; // init: STDIN
    FILE *fout;
    int nd; // init: 1
    int ifno;

    // options
    int pflag, passcom, rflag;

    // predefined -D / -U staging
    char *prespc[NPREDEF];
    char **predef; // init: prespc
    char *punspc[NPREDEF];
    char **prund; // init: punspc
    int exfail;

    // symbol table + directive/keyword handles
    struct symtab stab[symsiz];
    struct symtab *lastsym;
    struct symtab *defloc, *udfloc, *incloc, *ifloc, *elsloc, *eifloc, *ifdloc, *ifnloc, *ysysloc,
        *varloc, *lneloc, *ulnloc, *uflloc;

    // conditional-compilation nesting
    int trulvl, flslvl;

    // #if expression parser (parser.c / yylex.c)
    int yylval, lookahead, token_val;
};

extern struct cppstate cpp;

#define IB 1
#define SB 2
#define NB 4
#define CB 8
#define QB 16
#define WB 32

#define isid(a)   (cpp.fastab[(unsigned char)a] & IB)
#define isnum(a)  (cpp.fastab[(unsigned char)a] & NB)
#define iscom(a)  (cpp.fastab[(unsigned char)a] & CB)
#define isquo(a)  (cpp.fastab[(unsigned char)a] & QB)
#define iswarn(a) (cpp.fastab[(unsigned char)a] & WB)

int yylex(void);
int yyparse(void);
char *skipbl(char *p);
struct symtab *lookup(char *namep, int enterf);
void pperror(const char *s, ...);
void ppwarn(const char *s, ...);
