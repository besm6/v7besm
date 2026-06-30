//
// Linker for BESM-6 a.out objects.
// First pass: scan every object file and archive to collect the constant pool,
// segment sizes, and the global symbol table, and to parse the command line.
//
#include <stdlib.h>

#include "intern.h"

int load_constants(void)
{
    int count;
    int save;
    struct constab *c;
    const struct constab *p;

    save  = nconst;
    count = filhdr.a_const / W;
    c     = &constab[nconst];
    while (count--) {
        c->h   = fgeth(text);
        c->h2  = fgeth(text);
        c->hr  = fgeth(reloc);
        c->hr2 = fgeth(reloc);
        p      = c;
        if (!c->hr && !c->hr2)
            for (p = constab; p < c; p++)
                if (!p->hr2 && c->h == p->h && c->h2 == p->h2 && !p->hr)
                    break;
        if (p == c && ++c >= &constab[NCONST])
            error(2, "constant table overflow");
        newindex[cindex++] = p - constab;
    }
    nconst = c - constab;
    return nconst - save;
}

/*
 * single file
 */
int scan_object(long loc, int libflg, int nloc)
{
    struct nlist *sp;
    int savindex, savcindex;
    int ndef, nsymbol;

    read_header(loc);
    if (filhdr.a_flag & RELFLG) {
        error(1, "file stripped");
        return 0;
    }
    savcindex = cindex;
    fseek(reloc, loc + N_SYMOFF(filhdr), 0);
    coptsize[nfile] = load_constants();
    ctrel += tsize / W;
    cdrel += dsize / W;
    cbrel += bsize / W;
    carel += asize / W;
    loc += HDRSZ + (filhdr.a_const + filhdr.a_text + filhdr.a_data) * 2;
    fseek(text, loc, 0);
    ndef     = 0;
    savindex = symindex;
    if (nloc)
        nsymbol = 1;
    else
        nsymbol = 0;
    for (;;) {
        int symlen = fgetsym(text, &cursym);
        int type;
        if (symlen == 0)
            error(2, "out of memory");
        if (symlen == 1)
            break;
        type = cursym.n_type;
        if (Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_ACOMM)) {
            free(cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (!sflag && !xflag && (!Xflag || cursym.n_name[0] != LOCSYM)) {
                nsymbol++;
                nloc += symlen;
            }
            free(cursym.n_name);
            continue;
        }
        relocate_cursym();
        if (enter_symbol(lookup_symbol()))
            continue;
        free(cursym.n_name);
        if (cursym.n_type == N_EXT + N_UNDF)
            continue;
        sp = lastsym;
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM ||
            sp->n_type == N_EXT + N_ACOMM) {
            if (cursym.n_type == N_EXT + N_COMM || cursym.n_type == N_EXT + N_ACOMM) {
                sp->n_type = cursym.n_type;
                if (cursym.n_value > sp->n_value)
                    sp->n_value = cursym.n_value;
            } else if (sp->n_type == N_EXT + N_UNDF || cursym.n_type == N_EXT + N_DATA ||
                       cursym.n_type == N_EXT + N_BSS) {
                ndef++;
                sp->n_type  = cursym.n_type;
                sp->n_value = cursym.n_value;
            }
        }
    }
    if (!libflg || ndef) {
        csize = add_size(csize, (long)W * coptsize[nfile++], "const segment overflow");
        tsize = add_size(tsize, filhdr.a_text, "text segment overflow");
        dsize = add_size(dsize, filhdr.a_data, "data segment overflow");
        bsize = add_size(bsize, filhdr.a_bss, "bss segment overflow");
        asize = add_size_long(asize, filhdr.a_abss, "abss segment overflow");
        ssize = add_size(ssize, (long)nloc, "symbol table overflow");
        nsym += nsymbol;
        return 1;
    }

    /*
     * No symbols defined by this library member.
     * Rip out the hash table entries and reset the symbol table.
     */
    cindex = savcindex;
    nconst -= coptsize[nfile];
    while (symindex > savindex) {
        struct nlist **p;

        p = symhash[--symindex];
        free((*p)->n_name);
        *p = 0;
    }
    return 0;
}

/*
 * scan file to find defined symbols
 */
void scan_file(char *cp)
{
    long nloc;

    switch (open_input(cp)) {
    case 0: /* regular file */
        scan_object(0L, 0, make_file_symbol(cp, 0));
        break;
    case 1: /* regular archive */
        nloc = W;
    archive:
        while (scan_member(nloc))
            nloc += archdr.ar_size + ARHDRSZ;
        break;
    case 2: /* table of contents */
        read_ranlib();
        while (load_ranlib_members())
            ;
        free_ranlib();
        *libp++ = -1;
        check_liblist();
        break;
    case 3: /* out of date archive */
        error(0, "out of date (warning)");
        nloc = W + archdr.ar_size + ARHDRSZ;
        goto archive;
    }
    fclose(text);
    fclose(reloc);
}

/*
 * scan files once to find symdefs
 */
void pass1(int argc, char **argv)
{
    int c, i;
    long num;
    char *ap, **p;
    char save;

    p    = argv + 1;
    libp = liblist;
    for (c = 1; c < argc; ++c) {
        filname = 0;
        ap      = *p++;

        if (*ap != '-') {
            scan_file(ap);
            continue;
        }
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
                /* output file name */
            case 'o':
                if (++c >= argc)
                    error(2, "-o: argument missed");
                ofilename = *p++;
                ofilfnd++;
                continue;

                /* 'use' */
            case 'u':
                if (++c >= argc)
                    error(2, "-u: argument missed");
                enter_symbol(lookup_name(*p++));
                continue;

                /* 'entry' */
            case 'e':
                if (++c >= argc)
                    error(2, "-e: argument missed");
                enter_symbol(lookup_name(*p++));
                entrypt = lastsym;
                continue;

                /* set data size */
            case 'D':
                if (++c >= argc)
                    error(2, "-D: argument missed");
                num = W * atoi(*p++);
                if (dsize > num)
                    error(2, "-D: too small");
                dsize = num;
                continue;

                /* base address of loading */
            case 'T':
                basaddr = strtoul(ap + i + 1, 0, 0);
                break;

                /* library */
            case 'l':
                save  = ap[--i];
                ap[i] = '-';
                scan_file(&ap[i]);
                ap[i] = save;
                break;

                /* discard local symbols */
            case 'x':
                xflag++;
                continue;

                /* discard locals starting with LOCSYM */
            case 'X':
                Xflag++;
                continue;

                /* discard all except locals and globals*/
            case 'S':
                Sflag++;
                continue;

                /* put constants in data segment */
            case 'C':
                Cflag++;
                continue;

                /* preserve rel. bits, don't define common */
            case 'r':
                rflag++;
                arflag++;
                continue;

                /* discard all symbols */
            case 's':
                sflag++;
                xflag++;
                continue;

                /* pure procedure */
            case 'n':
                nflag++;
                continue;

                /* define common even with rflag */
            case 'd':
                dflag++;
                continue;

                /* tracing */
            case 't':
                trace++;
                continue;

            case 'k':
                alflag++;
                continue;

            default:
                error(2, "unknown flag");
            }
            break;
        }
    }
}
