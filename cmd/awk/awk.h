// Shared declarations.  Every object here is extern and defined in exactly one file;
// v7's tentative definitions do not survive C11.  README.md has the account.
//
// This was awk.def.  The name awk.h was v7's copy of y.tab.h, which this port does not
// make -- the token numbers come from y.tab.h itself.
#ifndef AWK_H
#define AWK_H

#include <stdlib.h>

typedef double awkfloat; // one 48-bit floating type here

// 2^40.  The mantissa is 40 bits, so past this every value is integral anyway and (long)
// is undefined -- an int is 41 bits.  lib/libc/gen/atof.c keys on the same number.
#define TWO40 1099511627776.0

#define xfree(a)         \
    {                    \
        if (a != NULL) { \
            free(a);     \
            a = NULL;    \
        }                \
    }

typedef struct val { // general value during processing
    char *nval;      // name, for variables only
    char *sval;      // string value
    awkfloat fval;   // value as number
    unsigned tval;   // type info
    struct val *nextval;
} cell;

#define STR 01  // string value is valid
#define NUM 02  // number value is valid
#define FLD 04  // FLD means don't free string space
#define CON 010 // this is a constant
#define ARR 020 // this is an array

typedef struct {
    char otype;
    char osub;
    cell *optr;
} obj;

#define BOTCH 1
struct nd {
    char ntype;
    char subtype;
    struct nd *nnext;
    int nobj;
    struct nd *narg[BOTCH]; // C won't take a zero length array
};
typedef struct nd node;

// The parser's value stack.  v7 left it int and let K&R pass pointers through it; almost
// every value is a node *, so that is what it holds.  README.md, "One fat pointer".
//
// A typedef and not `#define YYSTYPE node *': yacc writes `YYSTYPE yylval, yyval;', and
// the second declarator of a pointer #define is not a pointer.
typedef node *nodeptr;
#define YYSTYPE nodeptr

struct fa; // the regexp automaton; b.c owns it, the tree carries it as a node *

// otypes
#define OCELL 0
#define OEXPR 1
#define OBOOL 2
#define OJUMP 3

// cell subtypes
#define CTEMP 4
#define CNAME 3
#define CVAR  2
#define CFLD  1
#define CCON  0

// bool subtypes
#define BTRUE  1
#define BFALSE 2

// jump subtypes
#define JEXIT  1
#define JNEXT  2
#define JBREAK 3
#define JCONT  4

// node types
#define NVALUE 1
#define NSTAT  2
#define NEXPR  3
#define NPA2   4

// function types
#define FLENGTH 1
#define FSQRT   2
#define FEXP    3
#define FLOG    4
#define FINT    5

#define MAXSYM 50

// The internal encoding of ^ inside a regular expression.  v7 stole 0177, a byte a regexp
// may legitimately contain; 256 is outside the byte range and cannot collide.
#define HAT 256

// ---- globals, one defining file each ------------------------------------------------

// tran.c
extern cell *symtab[];
extern char **FS;
extern char **RS;
extern char **ORS;
extern char **OFS;
extern char **OFMT;
extern char **FILENAME;
extern awkfloat *NR;
extern awkfloat *NF;
extern cell *recloc;
extern cell *nrloc;
extern cell *nfloc;

// lib.c
extern char record[];
extern int errorflag;
extern int donefld; // 1 if record broken into fields
extern int donerec; // 1 if record is valid (no fld has changed)
extern int mustfld;

// awk.lx.l
extern int lineno;

// main.c
extern int svargc;
extern char **svargv;

// parse.c
extern node *nullstat;

// run.c
extern node *winner;
extern int pairstack[];
extern int paircnt;
extern obj awktrue, awkfalse; // v7's true and false

// proctab.c
typedef obj (*procfn)(node **, int);
extern procfn proctab[];

// ---- functions -----------------------------------------------------------------------

// b.c
struct fa *makedfa(node *p);
void penter(node *p);
void freetr(node *p);
int cclstash(char *s);
char *cclget(int h);
int cclenter(int h);
void overflo(void);
void cfoll(node *v);
int first(node *p);
void follow(node *v);
int member(int c, char *s);
int notin(int **arr, int n, int *prev);
int *add(int n);
struct fa *cgotofn(void);
int match(struct fa *pfa, char *p);

// lib.c
int getrec(void);
void fldinit(void);
void setclvar(char *s);
void fldbld(void);
void recbld(void);
cell *fieldadr(int n);
void yyerror(char *s); // b6yacc emits this prototype into y.tab.c; match it
void error(int f, const char *s, ...);
int isnumstr(char *s);

// parse.c
node *exptostat(node *a);
node *node1(int a, node *b);
node *node2(int a, node *b, node *c);
node *node3(int a, node *b, node *c, node *d);
node *node4(int a, node *b, node *c, node *d, node *e);
node *stat1(int a, node *b);
node *stat2(int a, node *b, node *c);
node *stat3(int a, node *b, node *c, node *d);
node *stat4(int a, node *b, node *c, node *d, node *e);
node *op1(int a, node *b);
node *op2(int a, node *b, node *c);
node *op3(int a, node *b, node *c, node *d);
node *valtonode(cell *a, int b);
node *pa2stat(node *a, node *b, node *c);
node *linkum(node *a, node *b);
node *genprint(void);

// proctab.c
void procinit(void);

// run.c
void run(void);
obj execute(node *u);
void tempfree(obj a);
obj gettemp(void);
obj nodetoobj(node *a);
obj nullproc(node **a, int n);
obj dopa2(node **a, int n);
char *format(char *s, node *a);
void redirprint(char *s, int a, node *b);
obj program(node **a, int n);
obj awkgetline(node **a, int n);
obj array(node **a, int n);
obj arrayel(node *a, obj b);
obj matchop(node **a, int n);
obj boolop(node **a, int n);
obj relop(node **a, int n);
obj indirect(node **a, int n);
obj substr(node **a, int n);
obj sindex(node **a, int n);
obj asprintf(node **a, int n);
obj arith(node **a, int n);
obj incrdecr(node **a, int n);
obj assign(node **a, int n);
obj cat(node **a, int n);
obj pastat(node **a, int n);
obj aprintf(node **a, int n);
obj split(node **a, int n);
obj ifstat(node **a, int n);
obj whilestat(node **a, int n);
obj forstat(node **a, int n);
obj instat(node **a, int n);
obj jump(node **a, int n);
obj fncn(node **a, int n);
obj print(node **a, int n);

// tran.c
void syminit(void);
cell **makesymtab(void);
void freesymtab(cell *ap);
cell *setsymtab(const char *n, char *s, awkfloat f, unsigned t, cell **tab);
int hash(const char *s);
cell *lookup(const char *s, cell **tab, unsigned flag);
awkfloat setfval(cell *vp, awkfloat f);
char *setsval(cell *vp, char *s);
awkfloat getfval(cell *vp);
char *getsval(cell *vp);
void checkval(cell *vp);
char *tostring(const char *s);

// the scanner, awk.lx.l
int yylex(void);
void startreg(void);
int yywrap(void);

// the parser, awk.g.y
int yyparse(void);

#define cantexec(n) (n->ntype == NVALUE)
#define notlegal(n) (n <= FIRSTTOKEN || n >= LASTTOKEN || proctab[n - FIRSTTOKEN] == nullproc)
#define isexpr(n)   (n->ntype == NEXPR)
#define isjump(n)   (n.otype == OJUMP)
#define isexit(n)   (n.otype == OJUMP && n.osub == JEXIT)
#define isbreak(n)  (n.otype == OJUMP && n.osub == JBREAK)
#define iscont(n)   (n.otype == OJUMP && n.osub == JCONT)
#define isnext(n)   (n.otype == OJUMP && n.osub == JNEXT)
#define isstr(n)    (n.optr->tval & STR)
#define istrue(n)   (n.otype == OBOOL && n.osub == BTRUE)
#define istemp(n)   (n.otype == OCELL && n.osub == CTEMP)
#define isfld(n)    (!donefld && n.osub == CFLD && n.otype == OCELL && n.optr->nval == 0)
#define isrec(n)    (donefld && n.osub == CFLD && n.otype == OCELL && n.optr->nval != 0)

#endif // AWK_H
