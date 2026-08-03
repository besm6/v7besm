//
// Linker for BESM-6 a.out objects.
// Output assembly: create the temporary segment buffers, write the executable
// header, and concatenate the buffers into the final image.
//
#include <stdlib.h>
#include <unistd.h>

#include "intern.h"

//
// Open one output buffer file into *buf.  With tempflg set it is a scratch file
// (one per segment) with no name on disk, so it disappears when it is closed;
// with tempflg clear it is the real output file.
//
// tmpfile() is C11 §7.21.4.3 and is exactly the fopen("w+") + unlink() this used
// to spell by hand around mkstemp().  It is also what the BESM-6 build needs:
// that libc has no mkstemp(), and its tmpnam() separates names by process id and
// a three-letter sequence, so the seven scratch streams open here never collide.
//
//
// Give one freshly opened stream a smaller buffer than stdio's default.  On the
// BESM-6 only: intern.h's LDBUFSIZ comment has the arithmetic, and on the host
// the default is both affordable and faster.  Must be called before any I/O on
// the stream, which is why every caller is right next to an fopen/tmpfile.
//
// A failure is not fatal -- setvbuf() leaves the stream unbuffered, which is
// slow but correct -- so there is nothing to report.
//
void shrink_buffer(FILE *f)
{
#if besm6
    setvbuf(f, NULL, _IOFBF, LDBUFSIZ);
#else
    (void)f;
#endif
}

void create_buffer(FILE **buf, int tempflg)
{
    *buf = tempflg ? tmpfile() : fopen(ld.ofilename, "w+");
    if (!*buf)
        error(2, tempflg ? "cannot create temporary file" : "cannot create output file");
    shrink_buffer(*buf);
}

//
// Prepare for pass 2: open the real output file plus a scratch buffer for each
// segment (and, with -r, for each segment's relocation), then fill in and write
// the executable header from the sizes and flags computed so far.  The segments
// are written to these separate buffers during pass 2 and stitched together by
// finish_output().  a_entry is the entry point: the -e symbol if given, else the
// start of text.
//
void setup_output(void)
{
    create_buffer(&ld.outb, 0);
    create_buffer(&ld.coutb, 1);
    create_buffer(&ld.toutb, 1);
    create_buffer(&ld.doutb, 1);
    if (!ld.sflag || !ld.xflag)
        create_buffer(&ld.soutb, 1);
    if (ld.rflag) {
        create_buffer(&ld.croutb, 1);
        create_buffer(&ld.troutb, 1);
        create_buffer(&ld.droutb, 1);
    }
    ld.filhdr.a_magic = ld.nflag ? NMAGIC : FMAGIC;
    ld.filhdr.a_const = ld.csize;
    // -n pads the text image up to the data origin (finish_output below), and that padding
    // is part of the file, so a_text has to count it: every reader -- b6sim's loader, b6nm,
    // and the kernel's getxfile()/xalloc() -- derives both the data segment's file offset
    // and its load address from const + text.  With the pad written but not declared, the
    // data was read from the middle of the padding and the whole image came up empty.
    ld.filhdr.a_text = ld.nflag ? (ld.dorigin - ld.torigin) * W : ld.tsize;
    ld.filhdr.a_data  = ld.dsize;
    ld.filhdr.a_bss   = ld.bsize;
    ld.filhdr.a_syms  = ALIGN(ld.ssize, W);
    if (ld.entrypt) {
        if (ld.entrypt->n_type != N_EXT + N_TEXT && ld.entrypt->n_type != N_EXT + N_UNDF)
            error(1, "entry out of text");
        else
            ld.filhdr.a_entry = ld.entrypt->n_value;
    } else
        ld.filhdr.a_entry = ld.torigin;
    if (ld.rflag)
        ld.filhdr.a_flag &= ~RELFLG;
    else
        ld.filhdr.a_flag |= RELFLG;
    fputhdr(&ld.filhdr, ld.outb);
}

//
// Append the entire contents of a scratch segment buffer to the final output
// file, then close (and thereby discard) the scratch buffer.
//
void copy_buffer(FILE *buf)
{
    int c;

    rewind(buf);
    while ((c = getc(buf)) != EOF)
        putc(c, ld.outb);
    fclose(buf);
}

//
// Assemble the final image after pass 2.  First, for a pure (-n) layout, pad
// the text up to the next page boundary.  Then append the segment buffers to
// the output in the order the header promised - const, text, data - optionally
// followed by the relocation buffers, and finally the symbol table.
//
void finish_output(void)
{
    if (ld.nflag) {
        long n;

        // Pad the text segment up to a page boundary.
        //
        // `ld.torigin' IS the end of the text here, not its start: pass2 advances torigin
        // and dorigin per input file (pass2.c), so by the time this runs they are cursors
        // sitting just past the last byte each segment received.  Which is why this pads
        // the right amount and why it must not be rewritten to look like it computes one
        // -- the matching a_text in setup_output above has to use the PRISTINE origins,
        // because it runs before pass2.
        n = ld.torigin;
        while (n & 01777) {
            n++;
            fputw(0, ld.toutb);
            if (ld.rflag) {
                fputw(0, ld.troutb);
            }
        }
    }

    // Concatenate the segment images in canonical order: const, text, data.
    copy_buffer(ld.coutb);
    copy_buffer(ld.toutb);
    copy_buffer(ld.doutb);

    // With -r, the relocation records follow in the same segment order.
    if (ld.rflag) {
        copy_buffer(ld.croutb);
        copy_buffer(ld.troutb);
        copy_buffer(ld.droutb);
    }

    // Finally the symbol table: the local symbols gathered in soutb, then every
    // global symbol, then a terminating zero byte padded out to a whole word.
    if (!ld.sflag) {
        const struct nlist *p;
        if (!ld.xflag)
            copy_buffer(ld.soutb);
        for (p = symtab; p < &symtab[ld.symindex]; ++p)
            fputsym(p, ld.outb);
        putc(0, ld.outb);
        while (ld.ssize++ % W)
            putc(0, ld.outb);
    }
    fclose(ld.outb);
}
