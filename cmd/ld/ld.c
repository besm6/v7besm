//
// Linker for micro-BESM.
//
// ---------------------------------------------------------------------------
// How this linker works (a primer for the rest of the code)
// ---------------------------------------------------------------------------
//
// A linker takes several object files (the ".o" output of the assembler) plus,
// optionally, libraries, and stitches them into one runnable program.  It has
// two jobs: (1) glue the matching pieces of every input together, and (2) fix
// up all the addresses so that, once everything sits at its final location, every
// reference points where it should.
//
// The BESM-6 it targets is a 48-bit *word* machine.  Memory is addressed by
// word, not by byte; one word is 6 bytes (the constant W below), so all sizes
// and file offsets are multiples of 6.  A machine instruction is a 24-bit
// half-word, two per word.  An "address" in this code is therefore a word index,
// not a byte pointer.
//
// Every object file is divided into four segments, and the linker glues each
// kind end-to-end across all the inputs:
//      const - read-only constants / literal pool
//      text  - the program code
//      data  - pre-initialized variables
//      bss   - variables that start out zero (occupy no space in the file)
//
// A symbol is a named location: it has a type (which segment it lives in) and a
// value (its address).  A *global* (external) symbol can be defined in one file
// and merely referenced - left "undefined" - in another; the linker's job is to
// match each reference to its definition.  A *common* symbol is a tentative
// definition (think of a C global with no initializer): several files may each
// request one, and the linker reserves a single slot sized to the largest.
//
// Relocation is step (2).  Each object is assembled as if its own segments
// started at address 0.  Once the linker has concatenated everybody and chosen a
// final base address for each segment, every address baked into the code/data is
// now wrong by exactly that base.  Each input carries *relocation records*, one
// per patchable address field, saying what the field points at (a particular
// segment, or a named external symbol) and how wide it is; the linker adds the
// right amount to each field to make it correct.
//
// All of that is done in two passes over the inputs:
//      pass 1 (pass1.c)  - read every file just to measure the segments and to
//                          build the global symbol table; assign_addresses()
//                          then lays the segments out and gives every symbol its
//                          final address.
//      pass 2 (pass2.c)  - read every file again, copy each segment's bytes into
//                          the output while relocating every address field, and
//                          finally write the header, segments and symbol table.
//
// The on-disk object/executable format (header, segment order, symbol and
// relocation records) is defined in cross/besm6/b.out.h - read that first if a
// field name like a_const or N_TEXT is unfamiliar.  All shared state lives in one
// struct (`struct linker ld`, declared in intern.h); the engine has no other
// globals, which is why nearly every line below touches some `ld.<field>`.
//
// ---------------------------------------------------------------------------
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
//      -r              preserve rel. bits, don't define common's
//      -s              discard all symbols
//      -n              pure procedure
//      -d              define common even with rflag
//      -t              tracing
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

//
// Same cleanup, but actually terminate the process.  This is what the signal
// handler (main.c) and a fatal error() call use; the engine's normal success
// path returns through ld_cleanup() instead so it can be unit-tested.
//
void cleanup_and_exit(void)
{
    exit(ld_cleanup());
}

//
// Report a problem.  `fmt`/`...` are printf-style.  `n` is the severity:
//      0 - warning; keep going, errlev stays 0
//      1 - error; remember it (errlev=1) but keep going so we can list every
//          problem in one run instead of stopping at the first
//      2 - fatal; print and abort immediately via cleanup_and_exit()
// The "ld: " prefix is printed only for the first message of a run, and the
// current input file name (ld.filname) is shown when known.
//
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

//
// Read and sanity-check the 8-word header of the object file at byte offset
// `loc` in ld.text, leaving it in ld.filhdr.  Each segment size must be a whole
// number of 6-byte words or the file is corrupt.
//
// It also precomputes this file's three relocation "biases" (ctrel/cdrel/
// cbrel).  Inside the object, a const-segment address counts from 0, a text
// address from 0, and so on - but the assembler actually stored them biased by
// BADDR and by the segments that precede them (const, then text, then data,
// then bss).  These biases are the amount to add to turn an in-file address back
// into a clean "offset within its own segment"; pass 2 later adds the segment's
// real base on top.  Negative because we are subtracting that built-in offset.
//
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
    ld.ctrel = -BADDR - ld.filhdr.a_const / W;
    ld.cdrel = -BADDR - (ld.filhdr.a_const + ld.filhdr.a_text) / W;
    ld.cbrel = -BADDR - (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data) / W;
}

//
// Return a+b, but abort if the running total `a` (a segment size in bytes)
// overflows the address range.  Used to accumulate the const/text/data/bss
// sizes as files are scanned.  `s` is the message printed on overflow.
//
// The limit is 0100000 (octal) words: the BESM-6 address space is a flat 15
// bits (32768 words), so no segment may grow past it.
//
long add_size(long a, long b, char *s)
{
    a += b;
    if (a >= 0100000L * W)
        error(1, s);
    return a;
}

//
// The "middle" pass, run once between pass 1 and pass 2.  By now pass 1 knows
// the total size of each segment (ld.csize/tsize/dsize/bsize) and has the
// whole global symbol table; this routine decides where everything finally goes:
//
//   1. Resolve the five built-in boundary symbols (_econst, _etext, ... _end) -
//      programs use these to find the end of each segment.
//   2. Decide whether the output stays relocatable (rflag): if any real symbol
//      is still undefined we keep relocation info so the file can be re-linked.
//   3. Reserve space for common symbols (one shared slot per name, see header).
//   4. Choose the base address of each segment and stack them in memory, then
//      add the right base to every symbol so its value becomes a final address.
//   5. Total up the symbol-table size (ssize) for the output file.
//
void assign_addresses(void)
{
    struct nlist *sp;
    const struct nlist *symp;
    long cmsize;
    int nund;
    long cmorigin;

    ld.p_econst = *lookup_name("_econst");
    ld.p_etext  = *lookup_name("_etext");
    ld.p_edata  = *lookup_name("_edata");
    ld.p_ebss   = *lookup_name("_ebss");
    ld.p_end    = *lookup_name("_end");

    //
    // If a genuine symbol is still undefined, we cannot produce a finished
    // executable, so force rflag: keep the relocation records in the output so
    // the file can be linked again later.  The five boundary symbols above don't
    // count - they are defined just below.
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
        ld.nflag = ld.sflag = 0;

    //
    // Lay out the common symbols.  A common symbol's n_value currently holds the
    // *size* the file asked for; replace it with the offset of the slot we hand
    // out, and grow the running common area (cmsize).  Skipped when -r without
    // -d, since then commons stay undefined for a later link.
    //
    cmsize  = 0;
    if (ld.dflag || !ld.rflag) {
        define_symbol(ld.p_econst, ld.csize / W, N_EXT + N_CONST);
        define_symbol(ld.p_etext, ld.tsize / W, N_EXT + N_TEXT);
        define_symbol(ld.p_edata, ld.dsize / W, N_EXT + N_DATA);
        define_symbol(ld.p_ebss, ld.bsize / W, N_EXT + N_BSS);
        define_symbol(ld.p_end, ld.bsize / W, N_EXT + N_BSS);
        for (sp = ld.symtab; sp < symp; sp++) {
            if ((sp->n_type & N_TYPE) == N_COMM) {
                long t      = sp->n_value;
                sp->n_value = cmsize / W;
                cmsize      = add_size(cmsize, (long)t * W, "bss segment overflow");
            }
        }
    }

    //
    // Stack the segments in memory, each starting where the previous one ended,
    // and record each one's base address (its "origin").  Normal order is:
    //
    //      corigin: const | torigin: text | dorigin: data | bss commons |
    //      borigin: bss
    //
    // -n forces the data origin up to the next 1024-word page boundary via ALIGN().
    //
    ld.corigin = ld.basaddr;
    ld.torigin = ld.corigin + ld.csize / W;
    ld.dorigin = ld.torigin + ld.tsize / W;
    if (ld.nflag) {
        ld.dorigin = ALIGN(ld.dorigin, 1024);
    }
    cmorigin    = ld.dorigin + ld.dsize / W; // bss commons sit right after data
    ld.borigin  = cmorigin + cmsize / W;     // then the files' own bss
    ld.cbasaddr = ld.corigin;

    //
    // Walk every global symbol and turn its segment-relative value into a final
    // address by adding that segment's origin.  Common symbols become ordinary
    // bss symbols here.  Undefined ones are reported (unless -r), and any
    // value that overflows the 27-bit address field is flagged.
    //
    nund = 0;
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
        case N_COMM:
        case N_EXT + N_COMM:
            sp->n_type = N_EXT + N_BSS;
            sp->n_value += cmorigin;
            break;
        }
        if (sp->n_value & ~077777777)
            error(1, "long address: %s=0%lo", sp->n_name, sp->n_value);
    }
    if (ld.sflag || ld.xflag)
        ld.ssize = 0;
    ld.bsize = add_size(ld.bsize, cmsize, "bss segment overflow");

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
    // Flush the buffers: concatenate the segments and symbol table into the
    // final image.
    //
    finish_output();

    //
    // The image was written to the temporary name "l.out".  If the user didn't
    // ask for a specific -o name, publish it as the conventional "a.out".
    //
    if (!ld.ofilfnd) {
        unlink("a.out");
        link("l.out", "a.out");
        ld.ofilename = "a.out";
    }
    ld.delarg = ld.errlev;
    return ld_cleanup();
}
