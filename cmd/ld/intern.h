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

#define W      6           // word length in bytes
#define LOCSYM 'L'         // discard local symbols starting with 'L'
#define BADDR  (HDRSZ / W) // memory 0...BADDR-1 is free
#define SYMDEF "__.SYMDEF"

#define NSYM     2000
#define NSYMPR   1000
#define NCONST   512
#define LLSIZE   256
#define RANTABSZ 1000

#define LNAMLEN 17 // originally 12

#define ALIGN(x, y) ((x) + (y) - 1 - ((x) + (y) - 1) % (y))

//
// symbol management
//
struct local {
    long locindex;           // index to symbol in file
    struct nlist *locsymbol; // ptr to symbol table
};

struct constab {
    long h, h2, hr, hr2;
};

//
// Global engine state (defined in ld.c).
//
struct linker {
    struct exec filhdr; // aout header
    struct ar_hdr archdr;
    FILE *text, *reloc; // input management

    // output management
    FILE *outb, *coutb, *toutb, *doutb, *croutb, *troutb, *droutb, *soutb;

    struct constab constab[NCONST]; // constants
    struct nlist cursym;            // current symbol
    struct nlist symtab[NSYM];      // the symbols themselves
    struct nlist **symhash[NSYM];   // pointers to hash table
    struct nlist *lastsym;          // last entered symbol
    struct nlist *hshtab[NSYM + 2]; // hash table for symbols
    struct local local[NSYMPR];
    int symindex;         // next free entry in symbol table
    int newindex[NCONST]; // constant reindexing table
    int nconst;           // next free entry in constab
    int cindex;           // current index in newindex
    int nfile;            // current file number (index into coptsize)
    int coptsize[LLSIZE]; // const segment lengths after optimization
    long basaddr;         // base address of loading
    struct ranlib rantab[RANTABSZ];
    int tnum; // number of elements in rantab

    long liblist[LLSIZE], *libp; // library management

    // internal symbols
    struct nlist *p_econst, *p_etext, *p_edata, *p_ebss, *p_end, *entrypt;

    // flags
    int trace;  // internal trace flag
    int xflag;  // discard local symbols
    int Xflag;  // discard locals starting with LOCSYM
    int Sflag;  // discard all except locals and globals
    int Cflag;  // put constants in data segment
    int rflag;  // preserve relocation bits, don't define commons
    int arflag; // original copy of rflag
    int sflag;  // discard all symbols
    int nflag;  // pure procedure
    int dflag;  // define common even with rflag
    int alflag; // const and text aligned on page boundary

    // cumulative sizes set in pass 1
    long csize, tsize, dsize, bsize, asize, ssize, nsym;

    // symbol relocation; both passes
    long ctrel, cdrel, cbrel, carel;

    int ofilfnd;
    char *ofilename;
    char *filname;
    int errlev;
    int delarg;
    char tfname[14];  // "/tmp/ldaXXXXX"
    char libname[44]; // "/usr/local/lib/microbesm/lib" + name

    // Needed after pass 1
    long corigin;
    long cbasaddr;
    long torigin;
    long dorigin;
    long borigin;
    long aorigin;
};

extern struct linker ld;

//
// Shared functions.
//
// ld.c
void error(int n, char *fmt, ...);
void read_header(long loc);
long add_size(long a, long b, char *s);
long add_size_long(long a, long b, char *s);
void assign_addresses(void);

// symtab.c
void relocate_cursym(void);
int enter_symbol(struct nlist **hp);
struct nlist **lookup_symbol(void);
struct nlist **lookup_name(char *s);
void define_symbol(struct nlist *sp, long val, int type);
struct nlist *lookup_local(const struct local *lp, int sn);
int make_file_symbol(char *s, int wflag);

// library.c
int open_input(char *cp);
void check_liblist(void);
int scan_member(long nloc);
void read_ranlib(void);
int load_ranlib_members(void);
void free_ranlib(void);

// pass1.c
int load_constants(void);
int scan_object(long loc, int libflg, int nloc);
void scan_file(char *cp);
void pass1(int argc, char **argv);

// pass2.c
void relocate_object(long loc);
void relocate_file(char *acp);
void pass2(int argc, char **argv);

// reloc.c
int reloc_type(int stype);
void relocate_halfword(const struct local *lp, long t, long r, long *pt, long *pr);
void relocate_constants(const struct local *lp);
void relocate_segment(const struct local *lp, FILE *b1, FILE *b2, long len);

// output.c
void create_buffer(FILE **buf, int tempflg);
void setup_output(void);
void copy_buffer(FILE *buf);
void finish_output(void);

#endif // BESM6_LD_INTERN_H
