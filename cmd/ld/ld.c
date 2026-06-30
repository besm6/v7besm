/*
 * Linker for micro-BESM.
 * Options:
 *      -o filename     output file name
 *      -u symbol       'use'
 *      -e symbol       'entry'
 *      -D size         set data size
 *      -Taddress       base address of loading
 *      -llibname       library
 *      -x              discard local symbols
 *      -X              discard locals starting with LOCSYM
 *      -S              discard all except locals and globals
 *      -C              put constants in data segment
 *      -r              preserve rel. bits, don't define common's
 *      -s              discard all symbols
 *      -n              pure procedure
 *      -d              define common even with rflag
 *      -t              tracing
 *      -k              align const and text on page boundary
 */
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "intern.h"

struct exec filhdr; /* aout header */
struct ar_hdr archdr;
FILE *text, *reloc; /* input management */

/*
 * output management
 */
FILE *outb, *coutb, *toutb, *doutb, *croutb, *troutb, *droutb, *soutb;

/*
 * symbol management
 */
struct constab constab[NCONST]; /* constants */

struct nlist cursym;            /* current symbol */
struct nlist symtab[NSYM];      /* the symbols themselves */
struct nlist **symhash[NSYM];   /* pointers to hash table */
struct nlist *lastsym;          /* last entered symbol */
struct nlist *hshtab[NSYM + 2]; /* hash table for symbols */
struct local local[NSYMPR];
int symindex;         /* next free entry in symbol table */
int newindex[NCONST]; /* constant reindexing table */
int nconst;           /* next free entry in constab */
int cindex;           /* current index in newindex */
int nfile;            /* current file number (index into coptsize) */
int coptsize[LLSIZE]; /* const segment lengths after optimization */
long basaddr = BADDR; /* base address of loading */
struct ranlib rantab[RANTABSZ];
int tnum; /* number of elements in rantab */

long liblist[LLSIZE], *libp; /* library management */

/*
 * internal symbols
 */
struct nlist *p_econst, *p_etext, *p_edata, *p_ebss, *p_end, *entrypt;

/*
 * flags
 */
int trace;  /* internal trace flag */
int xflag;  /* discard local symbols */
int Xflag;  /* discard locals starting with LOCSYM */
int Sflag;  /* discard all except locals and globals*/
int Cflag;  /* put constants in data segment */
int rflag;  /* preserve relocation bits, don't define commons */
int arflag; /* original copy of rflag */
int sflag;  /* discard all symbols */
int nflag;  /* pure procedure */
int dflag;  /* define common even with rflag */
int alflag; /* const and text aligned on page boundary */

/*
 * cumulative sizes set in pass 1
 */
long csize, tsize, dsize, bsize, asize, ssize, nsym;

/*
 * symbol relocation; both passes
 */
long ctrel, cdrel, cbrel, carel;

int ofilfnd;
char *ofilename = "l.out";
char *filname;
int errlev;
int delarg     = 4;
char tfname[]  = "/tmp/ldaXXXXX";
char libname[] = "/usr/local/lib/microbesm/libxxxxxxxxxxxxxxx";

/* Needed after pass 1 */
long corigin;
long cbasaddr;
long torigin;
long dorigin;
long borigin;
long aorigin;

/*
 * Final cleanup: remove the temporary l.out and set permissions on the
 * result.  Returns the exit code without calling exit(), so the engine
 * (ld_link) can be invoked from a test without terminating the process.
 */
static int ld_cleanup(void)
{
    unlink("l.out");
    if (!delarg && !arflag)
        chmod(ofilename, 0777 & ~umask(0));
    return delarg;
}

void cleanup_and_exit(void)
{
    exit(ld_cleanup());
}

void error(int n, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (!errlev)
        printf("ld: ");
    if (filname)
        printf("%s: ", filname);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (n > 1)
        cleanup_and_exit();
    errlev = n;
}

void read_header(long loc)
{
    fseek(text, loc, 0);
    if (!fgethdr(text, &filhdr))
        error(2, "bad format");
    if (filhdr.a_magic != FMAGIC)
        error(2, "bad magic");
    if (filhdr.a_const % W)
        error(2, "bad length of const");
    if (filhdr.a_text % W)
        error(2, "bad length of text");
    if (filhdr.a_data % W)
        error(2, "bad length of data");
    if (filhdr.a_bss % W)
        error(2, "bad length of bss");
    if (filhdr.a_abss % W)
        error(2, "bad length of abss");
    ctrel = -BADDR - filhdr.a_const / W;
    cdrel = -BADDR - (filhdr.a_const + filhdr.a_text) / W;
    cbrel = -BADDR - (filhdr.a_const + filhdr.a_text + filhdr.a_data) / W;
    carel = -BADDR - (filhdr.a_const + filhdr.a_text + filhdr.a_data + filhdr.a_bss) / W;
}

long add_size(long a, long b, char *s)
{
    a += b;
    if (a >= 04000000L * W)
        error(1, s);
    return a;
}

long add_size_long(long a, long b, char *s)
{
    a += b;
    if (a >= 01000000000L * W)
        error(1, s);
    return a;
}

void assign_addresses(void)
{
    struct nlist *sp;
    const struct nlist *symp;
    long cmsize, acmsize;
    int nund;
    long cmorigin, acmorigin;

    p_econst = *lookup_name("_econst");
    p_etext  = *lookup_name("_etext");
    p_edata  = *lookup_name("_edata");
    p_ebss   = *lookup_name("_ebss");
    p_end    = *lookup_name("_end");

    /*
     * If there are any undefined symbols, save the relocation bits.
     */
    symp = &symtab[symindex];
    if (!rflag) {
        for (sp = symtab; sp < symp; sp++)
            if (sp->n_type == N_EXT + N_UNDF && sp != p_end && sp != p_ebss && sp != p_edata &&
                sp != p_etext && sp != p_econst) {
                rflag++;
                dflag = 0;
                break;
            }
    }
    if (rflag)
        Cflag = alflag = nflag = sflag = 0;

    /*
     * Assign common locations.
     */

    cmsize  = 0;
    acmsize = 0;
    if (dflag || !rflag) {
        define_symbol(p_econst, csize / W, N_EXT + N_CONST);
        define_symbol(p_etext, tsize / W, N_EXT + N_TEXT);
        define_symbol(p_edata, dsize / W, N_EXT + N_DATA);
        define_symbol(p_ebss, bsize / W, N_EXT + N_BSS);
        define_symbol(p_end, asize / W, N_EXT + N_ABSS);
        for (sp = symtab; sp < symp; sp++) {
            long t;
            if ((sp->n_type & N_TYPE) == N_COMM) {
                t           = sp->n_value;
                sp->n_value = cmsize / W;
                cmsize      = add_size(cmsize, (long)t * W, "bss segment overflow");
            } else if ((sp->n_type & N_TYPE) == N_ACOMM) {
                t           = sp->n_value;
                sp->n_value = acmsize / W;
                acmsize     = add_size_long(acmsize, (long)t * W, "abss segment overflow");
            }
        }
    }

    /*
     * Now set symbols to their final value
     */
    if (Cflag)
        torigin = basaddr;
    else {
        corigin = basaddr;
        torigin = corigin + csize / W;
    }
    if (alflag)
        torigin = ALIGN(torigin, 1024);
    if (Cflag) {
        corigin = torigin + tsize / W;
        dorigin = corigin + csize / W;
    } else
        dorigin = torigin + tsize / W;
    if (nflag || alflag)
        dorigin = ALIGN(dorigin, 1024);
    cmorigin  = dorigin + dsize / W;
    borigin   = cmorigin + cmsize / W;
    acmorigin = borigin + bsize / W;
    aorigin   = acmorigin + acmsize / W;
    cbasaddr  = corigin;
    nund      = 0;
    for (sp = symtab; sp < symp; sp++) {
        switch (sp->n_type) {
        case N_EXT + N_UNDF:
            if (!arflag) {
                errlev |= 01;
                if (!nund)
                    printf("Undefined:\n");
                nund++;
                printf("\t%s\n", sp->n_name);
            }
            break;
        default:
        case N_EXT + N_ABS:
            break;
        case N_EXT + N_CONST:
            sp->n_value += corigin;
            break;
        case N_EXT + N_TEXT:
            sp->n_value += torigin;
            break;
        case N_EXT + N_DATA:
            sp->n_value += dorigin;
            break;
        case N_EXT + N_BSS:
            sp->n_value += borigin;
            break;
        case N_EXT + N_ABSS:
            sp->n_value += aorigin;
            break;
        case N_COMM:
        case N_EXT + N_COMM:
            sp->n_type = N_EXT + N_BSS;
            sp->n_value += cmorigin;
            break;
        case N_ACOMM:
        case N_EXT + N_ACOMM:
            sp->n_type = N_EXT + N_ABSS;
            sp->n_value += acmorigin;
            break;
        }
        if (sp->n_value & ~0777777777)
            error(1, "long address: %s=0%lo", sp->n_name, sp->n_value);
    }
    if (sflag || xflag)
        ssize = 0;
    bsize = add_size(bsize, cmsize, "bss segment overflow");
    asize = add_size_long(asize, acmsize, "abss segment overflow");

    /*
     * Compute ssize; add length of local symbols, if need,
     * and one more zero byte. Alignment will be taken at setup_output.
     */
    if (sflag)
        ssize = 0;
    else {
        if (xflag)
            ssize = 0;
        for (sp = symtab; sp < &symtab[symindex]; sp++)
            ssize += sp->n_len + 6;
        ssize++;
    }
}

/*
 * Linker engine: links the object files listed in argv and writes the
 * executable image.  Returns the error level (errlev); unlike the old main()
 * it does not call exit() on the success path, so it suits both the thin
 * front end (main.c) and the unit test.
 */
int ld_link(int argc, char **argv)
{
    /*
     * First pass: compute segment lengths, name table, and entry address.
     */
    pass1(argc, argv);
    filname = 0;

    /*
     * Process the name table.
     */
    assign_addresses();

    /*
     * Create buffer files and write the header.
     */
    setup_output();

    /*
     * Second pass: fix up references.
     */
    pass2(argc, argv);

    /*
     * Flush the buffers.
     */
    finish_output();

    if (!ofilfnd) {
        unlink("a.out");
        link("l.out", "a.out");
        ofilename = "a.out";
    }
    delarg = errlev;
    return ld_cleanup();
}
