//
// The sizes, the token codes, and every object the five sources share.
// once.h holds the definitions; lmain.c includes it exactly once.
// README.md is the account of what the port changed.
//
#ifndef LDEFS_H
#define LDEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// The alphabet is the whole byte.  README.md, "Eight bits".
#define NCH 256

// Largest class number cindex[], match[] and pchar[] hold; cclinter() refuses more.
#define MAXCCLASS 255

#define TOKENSIZE 1000
#define DEFSIZE   40
#define DEFCHAR   1000
#define STARTCHAR 100
#define STARTSIZE 256
#define CCLSIZE   1000
#define NACTIONS  100

// One size block; all five are overridable per-.l with %e %n %p %a %o.  The
// besm6 arm is measured, not guessed.  README.md, "Building for the BESM-6".
#if besm6
#   define TREESIZE 800  // awk.lx.l used 618 nodes
#   define NSTATES  288  // awk.lx.l made 202 states
#   define MAXPOS   1700 // awk.lx.l used 1,345 positions
#   define NTRANS   800  // awk.lx.l packed 530 transitions
#   define NOUTPUT  700  // awk.lx.l used 455 slots; the assert below floors it at 513
#else
#   define TREESIZE 1000
#   define NSTATES  500
#   define MAXPOS   2500
#   define NTRANS   2000
#   define NOUTPUT  3000
#endif

// The two depths the input chooses.  README.md, "Walking the tree".
#define MAXDEFER 400
#define MAXDEPTH 64

// Bytes of stdio buffer per stream on the BESM-6; 6*171, a whole number of
// words, as cmd/ld/intern.h's LDBUFSIZ.  README.md has the arithmetic.
#define LEXBUFSIZ 1026

// The invariants the sizes must satisfy; none was written down.
_Static_assert(TOKENSIZE > NCH, "token[] must hold a whole character class");
_Static_assert(CCLSIZE > NCH, "ccl[] must hold a whole character class");
_Static_assert(NOUTPUT > 2 * NCH, "NOUTPUT must clear layout()'s guard band");
_Static_assert(STARTSIZE >= 2 && STARTSIZE <= MAXCCLASS + 1,
               "a start condition number must fit one byte of slist[]");
_Static_assert(NSTATES >= 1 && NACTIONS >= 1, "empty size profile");
_Static_assert(NCH <= MAXCCLASS + 1, "a character must index one byte of match[]");

// Tree-node names.  A name below NCH is that character.  int name[], never a byte.
#define RCCL     (NCH + 90)
#define RNCCL    (NCH + 91)
#define RSTR     (NCH + 92)
#define RSCON    (NCH + 93)
#define RNEWE    (NCH + 94)
#define FINAL    (NCH + 95)
#define RNULLS   (NCH + 96)
#define RCAT     (NCH + 97)
#define STAR     (NCH + 98)
#define PLUS     (NCH + 99)
#define QUEST    (NCH + 100)
#define DIV      (NCH + 101)
#define BAR      (NCH + 102)
#define CARAT    (NCH + 103)
#define S1FINAL  (NCH + 104)
#define S2FINAL  (NCH + 105)

#define DEFSECTION  1
#define RULESECTION 2
#define ENDSECTION  5
#define TRUE        1
#define FALSE       0

// A STR value names one of yylex()'s two buffers, not a pointer into it.
// README.md, "An int is not a char *".
#define STR_NAME  0 // buf[], the definition being named
#define STR_TOKEN 1 // token[], everything else

extern int sargc;
extern char **sargv;
extern char buf[520];
extern int yyline; // line number of file
extern int sect;
extern int eof;
extern int lgatflg;
extern int divflg;
extern int funcflag;
extern int pflag;
extern int casecount;
extern int chset; // 1 = char set modified
extern FILE *fin, *fout, *fother, *errorf;
extern int fptr;
extern int prev; // previous input character
extern int pres; // present input character
extern int peek; // next input character

// The parse tree.  left[]/right[] hold a tree index, a character or an OFFSET --
// never a pointer.  RCCL/RNCCL: left[] indexes ccl[], then pchar[] after cfoll().
// RSCON: right[] indexes slist[].
extern int *name;
extern int *left;
extern int *right;
extern int *parent;
extern char *nullstr;
extern int tptr;

extern unsigned char pushc[TOKENSIZE];
extern unsigned char *pushptr;
extern unsigned char slist[STARTSIZE];
extern unsigned char *slptr;
extern char **def, **subs, *dchar;
extern char **sname, *schar;
extern unsigned char *ccl;
extern unsigned char *ccptr;
extern char *dp, *sp;
extern int dptr, sptr;
extern char *bptr; // store input position
extern char *tmpstat;
extern int count;
extern int **foll;
extern int *nxtpos;
extern int *positions;
extern int *gotof;
extern int *nexts;
extern unsigned char *nchar;
extern int **state;
extern int *sfall;          // fallback state num
extern char *cpackflg;      // true if state has been character packed
extern int *atable, aptr;
extern int nptr;
extern unsigned char symbol[NCH];
extern unsigned char cindex[NCH];
extern int xstate;
extern int stnum;
extern int ctable[NCH];
extern int ccount;
extern unsigned char match[NCH];
extern char extra[NACTIONS];
extern unsigned char *pcptr, *pchar;
extern int pchlen;
extern int nstates, maxpos;
extern int yytop;
extern int report;
extern int ntrans, treesize, outsize;
extern int rcount;
extern int optim;
extern int *verify, *advance, *stoff;
extern int scon;
extern int psave; // the `.' class, as an offset into ccl[]; -1 until built

// sub1.c
extern char *getl(char *p);
extern int space(int ch);
extern int digit(int c);
extern _Noreturn void error(char *fmt, ...);
extern void warning(char *fmt, ...);
extern int chpos(int c, const char *s);
extern int alpha(int c);
extern int printable(int c);
extern void lgate(void);
extern int siconv(const char *t);
extern int ctrans(char **ss);
extern void cclinter(int sw);
extern int usescape(int c);
extern int lookup(const char *s, char **t);
extern int cpyact(void);
extern int gch(void);
extern int mn2(int a, int d, int c);
extern int mn1(int a, int d);
extern int mn0(int a);
// v7's munput(t,p) took a character or a string through one untyped parameter.
extern void unputc(int c);
extern void unputs(const char *s);
extern int dupl(int n);

// sub2.c
extern void cfoll(int v);
extern void cgoto(void);
extern void acompute(int s);
extern void mkmatch(void);
extern void layout(void);

// header.c
extern void phead1(void);
extern void phead2(void);
extern void ptail(void);
extern void statistics(void);

// lmain.c
extern char *myalloc(int a, int b);
// Shrink one freshly opened stream's buffer; BESM-6 only, before any I/O on it.
extern void shrink_buffer(FILE *f);

// parser.y, through b6yacc
extern int yyparse(void);
extern int yylex(void);
extern void yyerror(char *s);
extern int yylval;

#endif // LDEFS_H
