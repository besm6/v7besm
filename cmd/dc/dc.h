/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// dc(1)'s number representation -- task C15.  README.md is the account.
//
// A number is a `struct blk': char cells holding decimal digit PAIRS 0-99, least significant
// first, topped by a cell of -1 when negative, ending in a scale byte.
//
// Plain char is unsigned here, so the cell readers sign-extend -- v7's own `#ifdef interdata'
// arm, now the only one.  sunputc does not: it reads the scale byte, which is 0-99.

#ifndef DC_H
#define DC_H

#include <stdio.h>  // FILE, EOF
#include <string.h> // memset, for zeroblk

// int, not size_t: v7 wrote `length(p) - PTRSZ' and `(c + PTRSZ) * PTRSZ', which an unsigned
// PTRSZ would turn into unsigned arithmetic that wraps rather than going negative.
#define BLK     ((int)sizeof(struct blk))
#define PTRSZ   ((int)sizeof(struct blk *))
#define HEADSZ  1024
#define STKSZ   100
#define RDSKSZ  100
#define TBLSZ   256
#define ARRAYST 0241
#define MAXIND  2048
#define NL      1
#define NG      2
#define NE      3

struct blk {
    char *rd;
    char *wt;
    char *beg;
    char *last;
};

// An array register (`:' and `;') holds block POINTERS where a number holds digits.  v7
// aliased the whole header with a second struct of `struct blk **' and moved its cursors
// through that; here a cursor is a fat char pointer whose marker sits above an int's 41
// bits, so word-typed arithmetic and ordering over the same words are both wrong.  Only the
// DEREFERENCE goes through the word view -- casting a byte cursor to a word pointer floors
// it, which is exact at byte offset 0 -- and every cursor motion stays in the byte view.
#define wdat(q) (*(struct blk **)(q))

// A whole slot is left to read.  v7 looped on rd == wt, which never comes true when the
// block's length is not a multiple of PTRSZ -- and `S' on an array name leaves exactly that.
#define morewd(p) ((p)->rd + PTRSZ <= (p)->wt)

// An element is 0 or a header this program allocated: a plain word pointer, so its address
// occupies bits 15-1 with nothing above.  A number's digit bytes read as a pointer put the
// first digit in bits 48-41 and cannot pass, which is what keeps a register filled by `s'
// from being walked as an array.
#define isblkptr(q) ((unsigned)(q) != 0 && (unsigned)(q) < 0100000)

_Static_assert(PTRSZ == (int)sizeof(int), "an array element must be exactly one word");

#define slen(p)    ((int)((p)->wt - (p)->beg))
#define zeroblk(p) memset((p)->beg, 0, (p)->last - (p)->beg)
#define srewind(p) (p)->rd = (p)->beg
#define screate(p) (p)->rd = (p)->wt = (p)->beg
#define fsfile(p)  (p)->rd = (p)->wt
#define strunc(p)  (p)->wt = (p)->rd
#define sfeof(p)   (((p)->rd == (p)->wt) ? 1 : 0)
#define sfbeg(p)   (((p)->rd == (p)->beg) ? 1 : 0)
#define sungetc(p, c) *(--(p)->rd) = c

// A cell at or above 0200 is a sign word; give it back as the negative int it was stored as.
#define NEGBYTE 0200
#define MASK    (-1 & ~0377)
#define sgetc(p)                                             \
    (((p)->rd == (p)->wt)                                    \
         ? EOF                                               \
         : ((((*(p)->rd) & NEGBYTE) != 0) ? (*(p)->rd++ | MASK) : *(p)->rd++))
#define slookc(p)                                            \
    (((p)->rd == (p)->wt)                                    \
         ? EOF                                               \
         : ((((*(p)->rd) & NEGBYTE) != 0) ? (*(p)->rd | MASK) : *(p)->rd))
#define sbackc(p)                                            \
    (((p)->rd == (p)->beg)                                   \
         ? EOF                                               \
         : ((((*(--(p)->rd)) & NEGBYTE) != 0) ? (*(p)->rd | MASK) : *(p)->rd))

#define sputc(p, c)          \
    {                        \
        if ((p)->wt == (p)->last) \
            sgrow(p);        \
        *(p)->wt++ = c;      \
    }
// rd may sit below wt: divide() and mult() over-write in place.  Hence `>' and not `!='.
#define salterc(p, c)        \
    {                        \
        if ((p)->rd == (p)->last) \
            sgrow(p);        \
        *(p)->rd++ = c;      \
        if ((p)->rd > (p)->wt) \
            (p)->wt = (p)->rd; \
    }
// The scale byte, never a sign word: no extension.
#define sunputc(p) (*((p)->rd = --(p)->wt))

#define OUTC(x)              \
    {                        \
        printf("%c", x);     \
        if (--count == 0) {  \
            printf("\\\n");   \
            count = ll;      \
        }                    \
    }
#define TEST2                \
    {                        \
        if ((count -= 2) <= 0) { \
            printf("\\\n");   \
            count = ll;      \
        }                    \
    }
#define EMPTY                        \
    if (stkerr != 0) {               \
        printf("stack empty\n");     \
        continue;                    \
    }
#define EMPTYR(x)                    \
    if (stkerr != 0) {               \
        pushp(x);                    \
        printf("stack empty\n");     \
        continue;                    \
    }
#define EMPTYS                       \
    if (stkerr != 0) {               \
        printf("stack empty\n");     \
        return (1);                  \
    }
#define EMPTYSR(x)                   \
    if (stkerr != 0) {               \
        printf("stack empty\n");     \
        pushp(x);                    \
        return (1);                  \
    }
#define error(p)     \
    {                \
        printf(p);   \
        continue;    \
    }
#define errorrt(p)   \
    {                \
        printf(p);   \
        return (1);  \
    }

// State.  All static: one translation unit, and `k'/`all'/`rel'/`count' are §1 names.
static struct blk *hfree;
static struct blk *arg1, *arg2;
static int svargc;
static int savk; // a scale, and at `*' the sum of two, so not a char
static char **svargv;
static FILE *curfile;
static struct blk *scalptr, *basptr, *tenptr, *inbas;
static struct blk *sqtemp, *chptr, *strptr, *divxyz;
static struct blk *stack[STKSZ];
static struct blk **stkptr, **stkbeg;
static struct blk **stkend;
static int stkerr;
static struct blk *readstk[RDSKSZ];
static struct blk **readptr;
static struct blk *rem;
static int k;
static struct blk *irem;
static int skd, skr;
static int expneg; // `neg' in v7, where bigot()'s local of that name shadowed it
static struct sym {
    struct sym *next;
    struct blk *val;
} symlst[TBLSZ];
static struct sym *stable[TBLSZ];
static struct sym *sptr, *sfree;
static FILE *fsave;
static long rel;
static long nbytes;
static long all;
static long headmor;
static long obase;
static int fw, fw1, ll;
static int logo;
static int logten; // `log10' in v7, a reserved library name here
static int count;
static char *dummy;

// Prototypes.  v7 had bare `struct blk *pop();' and implicit int; b6parse takes neither.
static void commnds(void);
static struct blk *divide(struct blk *ddivd, struct blk *ddivr);
static int dscale(void);
static struct blk *removr(struct blk *p, int n);
static struct blk *sqroot(struct blk *p);
static struct blk *expn(struct blk *base, struct blk *ex);
static void init(int argc, char **argv);
static void onintr(int sig);
static void pushp(struct blk *p);
static struct blk *pop(void);
static struct blk *readin(void);
static struct blk *add0(struct blk *p, int ct);
static struct blk *mult(struct blk *p, struct blk *q);
static void chsign(struct blk *p);
static int readc(void);
static void unreadc(int c);
static void binop(int c);
static void prtblk(struct blk *hptr);
static struct blk *getdec(struct blk *p, int sc);
static void tenot(struct blk *p, int sc);
static void oneot(struct blk *p, int sc, int ch);
static void hexot(struct blk *p, int flg);
static void bigot(struct blk *p, int flg);
static struct blk *add(struct blk *a1, struct blk *a2);
static int eqk(void);
static struct blk *removc(struct blk *p, int n);
static struct blk *scalint(struct blk *p);
static struct blk *scale(struct blk *p, int n);
static int subt(void);
static int command(void);
static int cond(int c);
static void load(void);
static int log2v(long n);
static struct blk *salloc(int size);
static struct blk *morehd(void);
static struct blk *copy(struct blk *hptr, int size);
static void sdump(char *s1, struct blk *hptr);
static void seekc(struct blk *hptr, int n);
static void salterwd(struct blk *hptr, struct blk *n);
static void sgrow(struct blk *hptr);
_Noreturn static void ospace(char *s);
static void garbage(void);
static void redef(struct blk *p);
static void release(struct blk *p);
static struct blk *getblkwd(struct blk *p);
static void putwd(struct blk *p, struct blk *c);
static struct blk *lookwd(struct blk *p);
static char *nalloc(char *p, int n, int have);
static int isstring(struct blk *p);

static void (*outdit)(struct blk *p, int flg); // hexot or bigot

#endif // DC_H
