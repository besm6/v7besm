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
#define LOCSYM '.'         // discard local symbols starting with '.'
#define BADDR  (HDRSZ / W) // memory 0...BADDR-1 is free
#define SYMDEF "__.SYMDEF" // name of the table-of-contents member in a ranlib archive

// THE BESM-6 PROFILE IS SMALLER, and it is measured rather than guessed: at the
// host sizes these tables come to ~24,300 words, against the 28,672 a user
// program gets for const+text+data+bss and heap together.  Linking this repo's
// own kernel, libc, sh, sed, cpp, as and ld, the high-water marks are 387 pooled
// constants, 386 const words in one object (besm6.o's interrupt vectors), 374
// global symbols, 105 local-symbol references and 331 ranlib entries.  The sizes
// below leave two to five times that.  README.md has the arithmetic.
//
// NCONST is the one NOT cut to the measurement: it is what caps the merged const
// pool, and CONSTTOP (07777) is the architectural ceiling above it, so a small
// value here would be a limit on what the native linker can link rather than on
// what it has been asked to link.  Packing struct constab is what pays for that.
#if besm6
#   define NSYM     1024 // capacity of the global symbol table -- measured max 374
#   define NSYMPR   256  // capacity of the per-file local-symbol map -- measured max 105
#   define NCONST   2048 // capacity of the merged const segment, in words (see CONSTTOP)
#   define NCINDEX  1024 // capacity of newindex[]: const words in ONE input file -- max 386
#   define LLSIZE   256  // capacity of the library / file-offset list
#   define RANTABSZ 512  // capacity of the ranlib table of contents -- measured max 331
#   define NLIBDIR  8    // capacity of the -L library search path
#else
#   define NSYM     2000 // capacity of the global symbol table
#   define NSYMPR   1000 // capacity of the per-file local-symbol map
#   define NCONST   4096 // capacity of the merged const segment, in words (see CONSTTOP)
#   define NCINDEX  4096 // capacity of newindex[]: const words in ONE input file
#   define LLSIZE   256  // capacity of the library / file-offset list
#   define RANTABSZ 1000 // capacity of the ranlib table of contents
#   define NLIBDIR  32   // capacity of the -L library search path
#endif

// Bytes of stdio buffer each of the linker's streams is given on the BESM-6.
//
// It has to be said explicitly, because the default is BUFSIZ -- 3,072 bytes,
// one disk block, 512 WORDS -- and the linker holds up to twelve streams open at
// once: the two handles on the input file, the output, one scratch file per
// segment, one for the symbol table, and under -r one more per segment's
// relocation records.  Twelve default buffers are 6,144 words of heap, and
// rootfs_ld_size cannot see a byte of it: the program links, then dies on a real
// link with "out of memory".  At the size below the same twelve cost ~2,050
// words.  What it buys back is more read(2)/write(2) calls, which is the right
// trade when the alternative is not running at all.
#define LDBUFSIZ 1024

// Round x up to the next multiple of y.
#define ALIGN(x, y) ((x) + (y) - 1 - ((x) + (y) - 1) % (y))

//
// symbol management
//
struct local {
    long locindex;           // index to symbol in file
    struct nlist *locsymbol; // ptr to symbol table
};

// One entry of the merged constant pool: a 48-bit const word (its two 24-bit
// half-words h and h2) and the two relocation half-words that go with it.
//
// All four are 24-bit quantities read straight off the disk by fgeth(), so each
// pair PACKS INTO ONE WORD -- high half first, the same big-endian order the
// format uses everywhere.  Four separate `long's would be four words, and this
// is the largest table in the linker: at NCONST it is the difference between
// 4,096 words of the BESM-6's address space and 8,192.  uword_t and not word_t
// because a native `int' is 41 bits and two 24-bit fields need 48.
struct constab {
    uword_t v; // the const word:       h  << 24 | h2
    uword_t r; // its relocation words: hr << 24 | hr2
};

#define CON_HI(w)      ((long)((w) >> 24))
#define CON_LO(w)      ((long)((w) & 077777777))
#define CON_PACK(a, b) (((uword_t)(a) << 24) | ((uword_t)(b) & 077777777))

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

    struct nlist cursym;   // scratch: the symbol just read from a file
    struct nlist *lastsym; // most recently looked-up / entered symbol
    int symindex;          // number of symbols used in symtab[]
    int nconst;            // number of constants used in constab[]
    int nfile;             // index of the current input file (into coptsize[])
    int tnum;              // number of entries used in rantab[]
    long basaddr;          // address where loading starts (the -T option)
    long *libp;            // current position within liblist[]
    int nlibdir;           // number of entries used in libdir[]

    // internal (linker-defined) symbols
    struct nlist *p_econst; // econst: first address past the const segment
    struct nlist *p_etext;  // etext:  first address past the text segment
    struct nlist *p_edata;  // edata:  first address past the data segment
    struct nlist *p_ebss;   // ebss:   first address past the bss segment
    struct nlist *p_end;    // end:    first address past everything (same as ebss)
    struct nlist *entrypt;  // the program entry point symbol (the -e option)

    // flags
    int trace;  // -t: print progress while linking
    int xflag;  // -x: discard local symbols
    int Xflag;  // -X: discard locals starting with LOCSYM
    int Sflag;  // -S: discard all except locals and globals
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
    long ssize; // symbol table
    long nsym;  // number of symbols emitted to the output

    // current file's relocation biases (set by read_header; see its comment)
    long ctrel; // const
    long cdrel; // data
    long cbrel; // bss

    int ofilfnd;      // set once a -o output name has been seen
    char *ofilename;  // output file name (default "l.out", then "a.out")
    char *progname;   // basename of argv[0], used as the diagnostic prefix
    char *filname;    // name of the input file currently being processed
    char *filname_alloc; // malloc'd base of filname for a "-l" arg (0 if filname is unowned)
    int errlev;       // highest error severity seen so far (the eventual exit code)
    int delarg;       // exit code; nonzero means "leave a.out alone / failed"

    // segment base addresses, fixed by assign_addresses() after pass 1
    long corigin;   // base of the const segment
    long cbasaddr;  // copy of corigin, used while relocating constants
    long torigin;   // base of the text segment
    long dorigin;   // base of the data segment
    long borigin;   // base of the bss segment
};

extern struct linker ld;

// The eight big tables live at FILE SCOPE rather than inside struct linker, and
// they have to: a struct member is named by a 12-bit offset from a base register
// and there is no longer form, so a struct above 4,096 words cannot be compiled
// at all (cmd/README.md SS6).  With these inside it, `ld' was ~50,400 words --
// twelve times the ceiling, and half again the whole address space.  A
// file-scope array is reached through an index register and has no such limit.
// b6as reports the failure as "short address out of range" against a line of
// generated assembly, which does not read like the cause at all.
//
// Moving them out of the struct changes nothing about their lifetime: they were
// static storage before and they are static storage now, zero at start-up, and
// the engine does ONE LINK PER PROCESS -- which is why hshtab[], the one table
// indexed by hash rather than below a counter, needs no reset (cmd/ld/test's
// header says the same, and gtest_discover_tests gives each case its own
// process for it).
extern struct constab constab[NCONST]; // the merged constant pool
extern struct nlist symtab[NSYM];      // the global symbol table
extern struct nlist **symhash[NSYM];   // for each symtab entry, its slot in hshtab
extern struct nlist *hshtab[NSYM + 2]; // open-addressing hash table over symtab
extern struct local local[NSYMPR];     // current file's local-symbol-number -> entry map
extern int newindex[NCINDEX];          // maps this file's constant index -> pooled index
extern int coptsize[LLSIZE];           // each file's const-pool size after de-duplication
extern struct ranlib rantab[RANTABSZ]; // a randomized archive's table of contents
extern long liblist[LLSIZE];           // file offsets of the archive members we load
extern char *libdir[NLIBDIR];          // -L: directories to search for -lNAME libraries

//
// Shared functions.
//
// ld.c
void error(int n, char *fmt, ...);
void read_header(long loc);
long add_size(long a, long b, char *s);
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
void shrink_buffer(FILE *f);
void create_buffer(FILE **buf, int tempflg);
void setup_output(void);
void copy_buffer(FILE *buf);
void finish_output(void);

#endif // BESM6_LD_INTERN_H
