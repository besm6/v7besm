//
// Linker for BESM-6 a.out objects.
// Internal shared declarations: global engine state and the prototypes of the
// functions split across ld.c, symtab.c, library.c, pass1.c, pass2.c, reloc.c
// and output.c.  This is a C-only header; the public, C++-safe entry point is
// in ld.h.
//
#ifndef BESM6_LD_INTERN_H
#define BESM6_LD_INTERN_H

#include <stdio.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"
#include "besm6/ranlib.h"

#include "ld.h"

#define W      6           /* word length in bytes */
#define LOCSYM 'L'         /* discard local symbols starting with 'L' */
#define BADDR  (HDRSZ / W) /* memory 0...BADDR-1 is free */
#define SYMDEF "__.SYMDEF"

#define NSYM     2000
#define NSYMPR   1000
#define NCONST   512
#define LLSIZE   256
#define RANTABSZ 1000

#define LNAMLEN 17 /* originally 12 */

#define ALIGN(x, y) ((x) + (y) - 1 - ((x) + (y) - 1) % (y))

/*
 * symbol management
 */
struct local {
    long locindex;           /* index to symbol in file */
    struct nlist *locsymbol; /* ptr to symbol table */
};

struct constab {
    long h, h2, hr, hr2;
};

/*
 * Global engine state (defined in ld.c).
 */
extern struct exec filhdr; /* aout header */
extern struct ar_hdr archdr;
extern FILE *text, *reloc; /* input management */

/* output management */
extern FILE *outb, *coutb, *toutb, *doutb, *croutb, *troutb, *droutb, *soutb;

extern struct constab constab[NCONST];  /* constants */
extern struct nlist cursym;             /* current symbol */
extern struct nlist symtab[NSYM];       /* the symbols themselves */
extern struct nlist **symhash[NSYM];    /* pointers to hash table */
extern struct nlist *lastsym;           /* last entered symbol */
extern struct nlist *hshtab[NSYM + 2];  /* hash table for symbols */
extern struct local local[NSYMPR];
extern int symindex;         /* next free entry in symbol table */
extern int newindex[NCONST]; /* constant reindexing table */
extern int nconst;           /* next free entry in constab */
extern int cindex;           /* current index in newindex */
extern int nfile;            /* current file number (index into coptsize) */
extern int coptsize[LLSIZE]; /* const segment lengths after optimization */
extern long basaddr;         /* base address of loading */
extern struct ranlib rantab[RANTABSZ];
extern int tnum; /* number of elements in rantab */

extern long liblist[LLSIZE], *libp; /* library management */

/* internal symbols */
extern struct nlist *p_econst, *p_etext, *p_edata, *p_ebss, *p_end, *entrypt;

/* flags */
extern int trace;  /* internal trace flag */
extern int xflag;  /* discard local symbols */
extern int Xflag;  /* discard locals starting with LOCSYM */
extern int Sflag;  /* discard all except locals and globals */
extern int Cflag;  /* put constants in data segment */
extern int rflag;  /* preserve relocation bits, don't define commons */
extern int arflag; /* original copy of rflag */
extern int sflag;  /* discard all symbols */
extern int nflag;  /* pure procedure */
extern int dflag;  /* define common even with rflag */
extern int alflag; /* const and text aligned on page boundary */

/* cumulative sizes set in pass 1 */
extern long csize, tsize, dsize, bsize, asize, ssize, nsym;

/* symbol relocation; both passes */
extern long ctrel, cdrel, cbrel, carel;

extern int ofilfnd;
extern char *ofilename;
extern char *filname;
extern int errlev;
extern int delarg;
extern char tfname[];
extern char libname[];

/* Needed after pass 1 */
extern long corigin;
extern long cbasaddr;
extern long torigin;
extern long dorigin;
extern long borigin;
extern long aorigin;

/*
 * Shared functions.
 */
/* ld.c */
void error(int n, char *fmt, ...);
void readhdr(long loc);
long add(long a, long b, char *s);
long addlong(long a, long b, char *s);
void middle(void);

/* symtab.c */
void symreloc(void);
int enter(struct nlist **hp);
struct nlist **lookup(void);
struct nlist **slookup(char *s);
void ldrsym(struct nlist *sp, long val, int type);
struct nlist *lookloc(const struct local *lp, int sn);
int mkfsym(char *s, int wflag);

/* library.c */
int getfile(char *cp);
void checklibp(void);
int step(long nloc);
void getrantab(void);
int ldrand(void);
void freerantab(void);

/* pass1.c */
int passconst(void);
int load1(long loc, int libflg, int nloc);
void load1arg(char *cp);
void pass1(int argc, char **argv);

/* pass2.c */
void load2(long loc);
void load2arg(char *acp);
void pass2(int argc, char **argv);

/* reloc.c */
int reltype(int stype);
void relhalf(const struct local *lp, long t, long r, long *pt, long *pr);
void relocconst(const struct local *lp);
void relocate(const struct local *lp, FILE *b1, FILE *b2, long len);

/* output.c */
void tcreat(FILE **buf, int tempflg);
void setupout(void);
void copy(FILE *buf);
void finishout(void);

#endif // BESM6_LD_INTERN_H
