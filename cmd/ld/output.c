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
// (one per segment) that is unlink()ed immediately, so it has no name on disk but
// stays usable until closed; with tempflg clear it is the real output file.
//
void create_buffer(FILE **buf, int tempflg)
{
    *buf = fopen(tempflg ? ld.tfname : ld.ofilename, "w+");
    if (!*buf)
        error(2, tempflg ? "cannot create temporary file" : "cannot create output file");
    if (tempflg)
        unlink(ld.tfname);
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
    int fd = mkstemp(ld.tfname);
    if (fd == -1) {
        error(2, "cannot create temporary file %s", ld.tfname);
    } else {
        close(fd);
    }
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
    ld.filhdr.a_text  = ld.tsize;
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
        for (p = ld.symtab; p < &ld.symtab[ld.symindex]; ++p)
            fputsym(p, ld.outb);
        putc(0, ld.outb);
        while (ld.ssize++ % W)
            putc(0, ld.outb);
    }
    fclose(ld.outb);
}
