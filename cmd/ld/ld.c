//
// Linker for micro-BESM.
// Options:
//      -o filename     output file name
//      -u symbol       'use'
//      -e symbol       'entry'
//      -D size         set data size
//      -Taddress       base address of loading
//      -llibname       library
//      -x              discard local symbols
//      -X              discard locals starting with LOCSYM
//      -S              discard all except locals and globals
//      -C              put constants in data segment
//      -r              preserve rel. bits, don't define common's
//      -s              discard all symbols
//      -n              pure procedure
//      -d              define common even with rflag
//      -t              tracing
//      -k              align const and text on page boundary
//
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "intern.h"

//
// The entire engine state, bundled into one struct (mirrors cmd/as).  Only the
// few fields below need a non-zero initial value; the rest are zeroed.
//
struct linker ld = {
    .basaddr   = BADDR,
    .ofilename = "l.out",
    .delarg    = 4,
    .tfname    = "/tmp/ldaXXXXX",
    .libname   = "/usr/local/lib/microbesm/libxxxxxxxxxxxxxxx",
};

//
// Final cleanup: remove the temporary l.out and set permissions on the
// result.  Returns the exit code without calling exit(), so the engine
// (ld_link) can be invoked from a test without terminating the process.
//
static int ld_cleanup(void)
{
    unlink("l.out");
    if (!ld.delarg && !ld.arflag)
        chmod(ld.ofilename, 0777 & ~umask(0));
    return ld.delarg;
}

void cleanup_and_exit(void)
{
    exit(ld_cleanup());
}

void error(int n, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (!ld.errlev)
        printf("ld: ");
    if (ld.filname)
        printf("%s: ", ld.filname);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (n > 1)
        cleanup_and_exit();
    ld.errlev = n;
}

void read_header(long loc)
{
    fseek(ld.text, loc, 0);
    if (!fgethdr(ld.text, &ld.filhdr))
        error(2, "bad format");
    if (ld.filhdr.a_magic != FMAGIC)
        error(2, "bad magic");
    if (ld.filhdr.a_const % W)
        error(2, "bad length of const");
    if (ld.filhdr.a_text % W)
        error(2, "bad length of text");
    if (ld.filhdr.a_data % W)
        error(2, "bad length of data");
    if (ld.filhdr.a_bss % W)
        error(2, "bad length of bss");
    if (ld.filhdr.a_abss % W)
        error(2, "bad length of abss");
    ld.ctrel = -BADDR - ld.filhdr.a_const / W;
    ld.cdrel = -BADDR - (ld.filhdr.a_const + ld.filhdr.a_text) / W;
    ld.cbrel = -BADDR - (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data) / W;
    ld.carel =
        -BADDR - (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data + ld.filhdr.a_bss) / W;
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

    ld.p_econst = *lookup_name("_econst");
    ld.p_etext  = *lookup_name("_etext");
    ld.p_edata  = *lookup_name("_edata");
    ld.p_ebss   = *lookup_name("_ebss");
    ld.p_end    = *lookup_name("_end");

    //
    // If there are any undefined symbols, save the relocation bits.
    //
    symp = &ld.symtab[ld.symindex];
    if (!ld.rflag) {
        for (sp = ld.symtab; sp < symp; sp++)
            if (sp->n_type == N_EXT + N_UNDF && sp != ld.p_end && sp != ld.p_ebss &&
                sp != ld.p_edata && sp != ld.p_etext && sp != ld.p_econst) {
                ld.rflag++;
                ld.dflag = 0;
                break;
            }
    }
    if (ld.rflag)
        ld.Cflag = ld.alflag = ld.nflag = ld.sflag = 0;

    //
    // Assign common locations.
    //

    cmsize  = 0;
    acmsize = 0;
    if (ld.dflag || !ld.rflag) {
        define_symbol(ld.p_econst, ld.csize / W, N_EXT + N_CONST);
        define_symbol(ld.p_etext, ld.tsize / W, N_EXT + N_TEXT);
        define_symbol(ld.p_edata, ld.dsize / W, N_EXT + N_DATA);
        define_symbol(ld.p_ebss, ld.bsize / W, N_EXT + N_BSS);
        define_symbol(ld.p_end, ld.asize / W, N_EXT + N_ABSS);
        for (sp = ld.symtab; sp < symp; sp++) {
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

    //
    // Now set symbols to their final value
    //
    if (ld.Cflag)
        ld.torigin = ld.basaddr;
    else {
        ld.corigin = ld.basaddr;
        ld.torigin = ld.corigin + ld.csize / W;
    }
    if (ld.alflag)
        ld.torigin = ALIGN(ld.torigin, 1024);
    if (ld.Cflag) {
        ld.corigin = ld.torigin + ld.tsize / W;
        ld.dorigin = ld.corigin + ld.csize / W;
    } else
        ld.dorigin = ld.torigin + ld.tsize / W;
    if (ld.nflag || ld.alflag)
        ld.dorigin = ALIGN(ld.dorigin, 1024);
    cmorigin    = ld.dorigin + ld.dsize / W;
    ld.borigin  = cmorigin + cmsize / W;
    acmorigin   = ld.borigin + ld.bsize / W;
    ld.aorigin  = acmorigin + acmsize / W;
    ld.cbasaddr = ld.corigin;
    nund        = 0;
    for (sp = ld.symtab; sp < symp; sp++) {
        switch (sp->n_type) {
        case N_EXT + N_UNDF:
            if (!ld.arflag) {
                ld.errlev |= 01;
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
            sp->n_value += ld.corigin;
            break;
        case N_EXT + N_TEXT:
            sp->n_value += ld.torigin;
            break;
        case N_EXT + N_DATA:
            sp->n_value += ld.dorigin;
            break;
        case N_EXT + N_BSS:
            sp->n_value += ld.borigin;
            break;
        case N_EXT + N_ABSS:
            sp->n_value += ld.aorigin;
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
    if (ld.sflag || ld.xflag)
        ld.ssize = 0;
    ld.bsize = add_size(ld.bsize, cmsize, "bss segment overflow");
    ld.asize = add_size_long(ld.asize, acmsize, "abss segment overflow");

    //
    // Compute ssize; add length of local symbols, if need,
    // and one more zero byte. Alignment will be taken at setup_output.
    //
    if (ld.sflag)
        ld.ssize = 0;
    else {
        if (ld.xflag)
            ld.ssize = 0;
        for (sp = ld.symtab; sp < &ld.symtab[ld.symindex]; sp++)
            ld.ssize += sp->n_len + 6;
        ld.ssize++;
    }
}

//
// Linker engine: links the object files listed in argv and writes the
// executable image.  Returns the error level (errlev); unlike the old main()
// it does not call exit() on the success path, so it suits both the thin
// front end (main.c) and the unit test.
//
int ld_link(int argc, char **argv)
{
    //
    // First pass: compute segment lengths, name table, and entry address.
    //
    pass1(argc, argv);
    ld.filname = 0;

    //
    // Process the name table.
    //
    assign_addresses();

    //
    // Create buffer files and write the header.
    //
    setup_output();

    //
    // Second pass: fix up references.
    //
    pass2(argc, argv);

    //
    // Flush the buffers.
    //
    finish_output();

    if (!ld.ofilfnd) {
        unlink("a.out");
        link("l.out", "a.out");
        ld.ofilename = "a.out";
    }
    ld.delarg = ld.errlev;
    return ld_cleanup();
}
