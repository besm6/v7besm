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
#define SYMDEF "__.SYMDEF" // name of the table-of-contents member in a ranlib archive

#define NSYM     2000 // capacity of the global symbol table
#define NSYMPR   1000 // capacity of the per-file local-symbol map
#define NCONST   512  // capacity of the merged constant pool
#define LLSIZE   256  // capacity of the library / file-offset list
#define RANTABSZ 1000 // capacity of the ranlib table of contents

#define LNAMLEN 17 // originally 12

// Round x up to the next multiple of y.
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
    struct exec filhdr;   // header of the object file currently being read
    struct ar_hdr archdr; // header of the archive member currently being read
    FILE *text;           // current input file, opened for the segment/symbol data
    FILE *reloc;           // a second handle on the same file, for its relocation records

    // The output is built in one temp file per segment, then concatenated at the
    // end.  The "r" variants hold the matching relocation records (only with -r).
    FILE *outb;   // the final output file
    FILE *coutb;  // const segment
    FILE *toutb;  // text segment
    FILE *doutb;  // data segment
    FILE *croutb; // const relocation
    FILE *troutb; // text relocation
    FILE *droutb; // data relocation
    FILE *soutb;  // symbol table

    struct constab constab[NCONST]; // the merged constant pool
    struct nlist cursym;            // scratch: the symbol just read from a file
    struct nlist symtab[NSYM];      // the global symbol table
    struct nlist **symhash[NSYM];   // for each symtab entry, its slot in hshtab
    struct nlist *lastsym;          // most recently looked-up / entered symbol
    struct nlist *hshtab[NSYM + 2]; // open-addressing hash table over symtab
    struct local local[NSYMPR];     // current file's local-symbol-number -> entry map
    int symindex;                   // number of symbols used in symtab
    int newindex[NCONST];           // maps a file's constant index -> pooled index
    int nconst;                     // number of constants used in constab
    int cindex;                     // current write position in newindex
    int nfile;                      // index of the current input file (into coptsize)
    int coptsize[LLSIZE];           // each file's const-pool size after de-duplication
    long basaddr;                   // address where loading starts (the -T option)
    struct ranlib rantab[RANTABSZ]; // a randomized archive's table of contents
    int tnum;                       // number of entries used in rantab

    long liblist[LLSIZE]; // file offsets of the archive members we decided to load
    long *libp;           // current position within liblist

    // internal (linker-defined) symbols
    struct nlist *p_econst; // _econst: first address past the const segment
    struct nlist *p_etext;  // _etext:  first address past the text segment
    struct nlist *p_edata;  // _edata:  first address past the data segment
    struct nlist *p_ebss;   // _ebss:   first address past the bss segment
    struct nlist *p_end;    // _end:    first address past everything
    struct nlist *entrypt;  // the program entry point symbol (the -e option)

    // flags
    int trace;  // -t: print progress while linking
    int xflag;  // -x: discard local symbols
    int Xflag;  // -X: discard locals starting with LOCSYM
    int Sflag;  // -S: discard all except locals and globals
    int Cflag;  // -C: put constants in the data segment
    int rflag;  // -r: keep relocation bits, don't define commons
    int arflag; // original copy of rflag (rflag may be forced on later)
    int sflag;  // -s: discard all symbols
    int nflag;  // -n: pure procedure (read-only text)
    int dflag;  // -d: define commons even with -r

    // running segment sizes, totalled during pass 1 (bytes)
    long csize; // const segment
    long tsize; // text segment
    long dsize; // data segment
    long bsize; // bss segment
    long asize; // abss segment
    long ssize; // symbol table
    long nsym;  // number of symbols emitted to the output

    // current file's relocation biases (set by read_header; see its comment)
    long ctrel; // const
    long cdrel; // data
    long cbrel; // bss
    long carel; // abss

    int ofilfnd;      // set once a -o output name has been seen
    char *ofilename;  // output file name (default "l.out", then "a.out")
    char *filname;    // name of the input file currently being processed
    int errlev;       // highest error severity seen so far (the eventual exit code)
    int delarg;       // exit code; nonzero means "leave a.out alone / failed"
    char tfname[14];  // template for the temp files: "/tmp/ldaXXXXX"
    char libname[44]; // scratch buffer for building a "-l" library path

    // segment base addresses, fixed by assign_addresses() after pass 1
    long corigin;   // base of the const segment
    long cbasaddr;  // copy of corigin, used while relocating constants
    long torigin;   // base of the text segment
    long dorigin;   // base of the data segment
    long borigin;   // base of the bss segment
    long aorigin;   // base of the abss segment
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
