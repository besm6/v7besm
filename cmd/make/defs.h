// make(1) -- the shared declarations.  README.md has the account of the port.

#ifndef MAKE_DEFS_H
#define MAKE_DEFS_H

#include <dirent.h>
#include <stdio.h>

#define SHELLCOM "/bin/sh"

typedef long TIMETYPE;

#define NO  0
#define YES 1

#define unequal strcmp

#define HASHSIZE 509
#define NLEFTS   40
#define INMAX    1500 // longest input line
#define OUTMAX   2500 // longest command line after substitution
#define QBUFMAX  1500 // longest $? list

// doname() recurses once per level of the dependency graph and its frame is 214
// words of the 4,096-word stack.  README.md, "The stack", has the arithmetic.
#define MAXLEVEL 11

// and subst() recurses once per nested macro reference, at 116 words a level.
#define MAXSUBST 20

#define ALLDEPS  1
#define SOMEDEPS 2

#define META     01
#define TERMINAL 02

// Indexed by a char, and a char here is unsigned: 256 entries, not v7's 128.
extern char funny[256];

#define ALLOC(x) (struct x *)ckalloc(sizeof(struct x))

struct nameblock {
    struct nameblock *nxtnameblock;
    char *namep;
    struct lineblock *linep;
    int done; // v7 packed done and septype into three bits each
    int septype;
    TIMETYPE modtime;
};

struct lineblock {
    struct lineblock *nxtlineblock;
    struct depblock *depp;
    struct shblock *shp;
};

struct depblock {
    struct depblock *nxtdepblock;
    struct nameblock *depname;
};

struct shblock {
    struct shblock *nxtshblock;
    char *shbp;
};

struct varblock {
    struct varblock *nxtvarblock;
    char *varname;
    char *varval;
    int noreset;
    int used;
};

struct pattern {
    struct pattern *nxtpattern;
    char *patval;
};

// One open directory, kept open and rewound rather than reopened.
struct opendir {
    struct opendir *nxtopendir;
    DIR *dirfc;
    char *dirn;
};

struct chain {
    struct chain *nextp;
    char *datap;
};

// main.c
extern struct nameblock *mainname;
extern struct nameblock *firstname;
extern struct lineblock *sufflist;
extern struct varblock *firstvar;
extern struct pattern *firstpat;
extern struct opendir *firstod;

extern int sigivalue;
extern int sigqvalue;
extern int childpid; // the child await() is waiting for; v7 called it waitpid
extern int dbgflag;
extern int prtrflag;
extern int silflag;
extern int noexflag;
extern int keepgoing;
extern int noruleflag;
extern int touchflag;
extern int questflag;
extern int ndocoms;
extern int ignerr;
extern int okdel;
extern int inarglist;
extern char *prompt;
extern char junkname[20];
extern char **linesptr;
extern FILE *fin;

void enbint(void (*k)(int));
void intrupt(int sig);
int isprecious(char *p);
int rddescf(char *descfile);
void printdesc(int prntflag);

// gram.y
extern char *zznextc;
extern int yylineno;
int yyparse(void);
int yylex(void);

// files.c
extern char *builtin[];
TIMETYPE exists(char *filename);
TIMETYPE prestime(void);
struct depblock *srchdir(char *pat, int mkchain, struct depblock *nextdbl);

// doname.c
int doname(struct nameblock *p, int reclevel, TIMETYPE *tval);
int docom(struct shblock *q);
void expand(struct depblock *q);

// dosys.c
int dosys(char *comstring, int nohalt);
void doclose(void);
void touch(int force, char *name);

// misc.c
struct nameblock *srchname(char *s);
struct nameblock *makename(char *s);
char *copys(char *s);
char *concat(char *a, char *b, char *c);
int suffix(char *a, char *b, char *p);
void *ckalloc(int n);
char *subst(char *a, char *b, char *bend); // bend: last writable byte of b
void setvar(char *v, char *s);
int eqsign(char *a);
struct varblock *varptr(char *v);
void fatal1(char *s, char *t);
void fatal(char *s);
void yyerror(char *s);
struct chain *appendq(struct chain *head, char *tail);
char *mkqlist(struct chain *p);

#endif // MAKE_DEFS_H
