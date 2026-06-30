//
// Linker for BESM-6 a.out objects.
// Output assembly: create the temporary segment buffers, write the executable
// header, and concatenate the buffers into the final image.
//
#include <unistd.h>

#include "intern.h"

void tcreat(FILE **buf, int tempflg)
{
    *buf = fopen(tempflg ? tfname : ofilename, "w+");
    if (!*buf)
        error(2, tempflg ? "cannot create temporary file" : "cannot create output file");
    if (tempflg)
        unlink(tfname);
}

void setupout(void)
{
    int fd = mkstemp(tfname);
    if (fd == -1) {
        error(2, "cannot create temporary file %s", tfname);
    } else {
        close(fd);
    }
    tcreat(&outb, 0);
    tcreat(&coutb, 1);
    tcreat(&toutb, 1);
    tcreat(&doutb, 1);
    if (!sflag || !xflag)
        tcreat(&soutb, 1);
    if (rflag) {
        tcreat(&croutb, 1);
        tcreat(&troutb, 1);
        tcreat(&droutb, 1);
    }
    filhdr.a_magic = nflag ? NMAGIC : alflag ? AMAGIC : FMAGIC;
    filhdr.a_const = csize;
    filhdr.a_text  = tsize;
    filhdr.a_data  = dsize;
    filhdr.a_bss   = bsize;
    filhdr.a_abss  = asize;
    filhdr.a_syms  = ALIGN(ssize, W);
    if (entrypt) {
        if (entrypt->n_type != N_EXT + N_TEXT && entrypt->n_type != N_EXT + N_UNDF)
            error(1, "entry out of text");
        else
            filhdr.a_entry = entrypt->n_value;
    } else
        filhdr.a_entry = torigin;
    if (rflag)
        filhdr.a_flag &= ~RELFLG;
    else
        filhdr.a_flag |= RELFLG;
    if (Cflag)
        filhdr.a_flag |= TCDFLG;
    else
        filhdr.a_flag &= ~TCDFLG;
    fputhdr(&filhdr, outb);
}

void copy(FILE *buf)
{
    int c;

    rewind(buf);
    while ((c = getc(buf)) != EOF)
        putc(c, outb);
    fclose(buf);
}

void finishout(void)
{
    if (nflag || alflag) {
        long n;
        if (alflag) {
            n = corigin;
            while (n & 01777) {
                n++;
                fputw(0, coutb);
            }
        }
        /* now torigin points to the end of text */
        n = torigin;
        while (n & 01777) {
            n++;
            fputw(0, toutb);
            if (rflag) {
                fputw(0, troutb);
            }
        }
    }
    if (!Cflag)
        copy(coutb);
    copy(toutb);
    if (Cflag)
        copy(coutb);
    copy(doutb);
    if (rflag) {
        if (!Cflag)
            copy(croutb);
        copy(troutb);
        if (Cflag)
            copy(croutb);
        copy(droutb);
    }
    if (!sflag) {
        const struct nlist *p;
        if (!xflag)
            copy(soutb);
        for (p = symtab; p < &symtab[symindex]; ++p)
            fputsym(p, outb);
        putc(0, outb);
        while (ssize++ % W)
            putc(0, outb);
    }
    fclose(outb);
}
