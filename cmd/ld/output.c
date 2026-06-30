//
// Linker for BESM-6 a.out objects.
// Output assembly: create the temporary segment buffers, write the executable
// header, and concatenate the buffers into the final image.
//
#include <unistd.h>

#include "intern.h"

void create_buffer(FILE **buf, int tempflg)
{
    *buf = fopen(tempflg ? ld.tfname : ld.ofilename, "w+");
    if (!*buf)
        error(2, tempflg ? "cannot create temporary file" : "cannot create output file");
    if (tempflg)
        unlink(ld.tfname);
}

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
    ld.filhdr.a_magic = ld.nflag ? NMAGIC : ld.alflag ? AMAGIC : FMAGIC;
    ld.filhdr.a_const = ld.csize;
    ld.filhdr.a_text  = ld.tsize;
    ld.filhdr.a_data  = ld.dsize;
    ld.filhdr.a_bss   = ld.bsize;
    ld.filhdr.a_abss  = ld.asize;
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
    if (ld.Cflag)
        ld.filhdr.a_flag |= TCDFLG;
    else
        ld.filhdr.a_flag &= ~TCDFLG;
    fputhdr(&ld.filhdr, ld.outb);
}

void copy_buffer(FILE *buf)
{
    int c;

    rewind(buf);
    while ((c = getc(buf)) != EOF)
        putc(c, ld.outb);
    fclose(buf);
}

void finish_output(void)
{
    if (ld.nflag || ld.alflag) {
        long n;
        if (ld.alflag) {
            n = ld.corigin;
            while (n & 01777) {
                n++;
                fputw(0, ld.coutb);
            }
        }
        // now torigin points to the end of text
        n = ld.torigin;
        while (n & 01777) {
            n++;
            fputw(0, ld.toutb);
            if (ld.rflag) {
                fputw(0, ld.troutb);
            }
        }
    }
    if (!ld.Cflag)
        copy_buffer(ld.coutb);
    copy_buffer(ld.toutb);
    if (ld.Cflag)
        copy_buffer(ld.coutb);
    copy_buffer(ld.doutb);
    if (ld.rflag) {
        if (!ld.Cflag)
            copy_buffer(ld.croutb);
        copy_buffer(ld.troutb);
        if (ld.Cflag)
            copy_buffer(ld.croutb);
        copy_buffer(ld.droutb);
    }
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
