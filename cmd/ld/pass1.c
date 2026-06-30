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

    save  = ld.nconst;
    count = ld.filhdr.a_const / W;
    c     = &ld.constab[ld.nconst];
    while (count--) {
        c->h   = fgeth(ld.text);
        c->h2  = fgeth(ld.text);
        c->hr  = fgeth(ld.reloc);
        c->hr2 = fgeth(ld.reloc);
        p      = c;
        if (!c->hr && !c->hr2)
            for (p = ld.constab; p < c; p++)
                if (!p->hr2 && c->h == p->h && c->h2 == p->h2 && !p->hr)
                    break;
        if (p == c && ++c >= &ld.constab[NCONST])
            error(2, "constant table overflow");
        ld.newindex[ld.cindex++] = p - ld.constab;
    }
    ld.nconst = c - ld.constab;
    return ld.nconst - save;
}

//
// single file
//
int scan_object(long loc, int libflg, int nloc)
{
    struct nlist *sp;
    int savindex, savcindex;
    int ndef, nsymbol;

    read_header(loc);
    if (ld.filhdr.a_flag & RELFLG) {
        error(1, "file stripped");
        return 0;
    }
    savcindex = ld.cindex;
    fseek(ld.reloc, loc + N_SYMOFF(ld.filhdr), 0);
    ld.coptsize[ld.nfile] = load_constants();
    ld.ctrel += ld.tsize / W;
    ld.cdrel += ld.dsize / W;
    ld.cbrel += ld.bsize / W;
    ld.carel += ld.asize / W;
    loc += HDRSZ + (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data) * 2;
    fseek(ld.text, loc, 0);
    ndef     = 0;
    savindex = ld.symindex;
    if (nloc)
        nsymbol = 1;
    else
        nsymbol = 0;
    for (;;) {
        int symlen = fgetsym(ld.text, &ld.cursym);
        int type;
        if (symlen == 0)
            error(2, "out of memory");
        if (symlen == 1)
            break;
        type = ld.cursym.n_type;
        if (ld.Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_ACOMM)) {
            free(ld.cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (!ld.sflag && !ld.xflag && (!ld.Xflag || ld.cursym.n_name[0] != LOCSYM)) {
                nsymbol++;
                nloc += symlen;
            }
            free(ld.cursym.n_name);
            continue;
        }
        relocate_cursym();
        if (enter_symbol(lookup_symbol()))
            continue;
        free(ld.cursym.n_name);
        if (ld.cursym.n_type == N_EXT + N_UNDF)
            continue;
        sp = ld.lastsym;
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM ||
            sp->n_type == N_EXT + N_ACOMM) {
            if (ld.cursym.n_type == N_EXT + N_COMM || ld.cursym.n_type == N_EXT + N_ACOMM) {
                sp->n_type = ld.cursym.n_type;
                if (ld.cursym.n_value > sp->n_value)
                    sp->n_value = ld.cursym.n_value;
            } else if (sp->n_type == N_EXT + N_UNDF || ld.cursym.n_type == N_EXT + N_DATA ||
                       ld.cursym.n_type == N_EXT + N_BSS) {
                ndef++;
                sp->n_type  = ld.cursym.n_type;
                sp->n_value = ld.cursym.n_value;
            }
        }
    }
    if (!libflg || ndef) {
        ld.csize = add_size(ld.csize, (long)W * ld.coptsize[ld.nfile++], "const segment overflow");
        ld.tsize = add_size(ld.tsize, ld.filhdr.a_text, "text segment overflow");
        ld.dsize = add_size(ld.dsize, ld.filhdr.a_data, "data segment overflow");
        ld.bsize = add_size(ld.bsize, ld.filhdr.a_bss, "bss segment overflow");
        ld.asize = add_size_long(ld.asize, ld.filhdr.a_abss, "abss segment overflow");
        ld.ssize = add_size(ld.ssize, (long)nloc, "symbol table overflow");
        ld.nsym += nsymbol;
        return 1;
    }

    //
    // No symbols defined by this library member.
    // Rip out the hash table entries and reset the symbol table.
    //
    ld.cindex = savcindex;
    ld.nconst -= ld.coptsize[ld.nfile];
    while (ld.symindex > savindex) {
        struct nlist **p;

        p = ld.symhash[--ld.symindex];
        free((*p)->n_name);
        *p = 0;
    }
    return 0;
}

//
// scan file to find defined symbols
//
void scan_file(char *cp)
{
    long nloc;

    switch (open_input(cp)) {
    case 0: // regular file
        scan_object(0L, 0, make_file_symbol(cp, 0));
        break;
    case 1: // regular archive
        nloc = W;
    archive:
        while (scan_member(nloc))
            nloc += ld.archdr.ar_size + ARHDRSZ;
        break;
    case 2: // table of contents
        read_ranlib();
        while (load_ranlib_members())
            ;
        free_ranlib();
        *ld.libp++ = -1;
        check_liblist();
        break;
    case 3: // out of date archive
        error(0, "out of date (warning)");
        nloc = W + ld.archdr.ar_size + ARHDRSZ;
        goto archive;
    }
    fclose(ld.text);
    fclose(ld.reloc);
}

//
// scan files once to find symdefs
//
void pass1(int argc, char **argv)
{
    int c, i;
    long num;
    char *ap, **p;
    char save;

    p       = argv + 1;
    ld.libp = ld.liblist;
    for (c = 1; c < argc; ++c) {
        ld.filname = 0;
        ap         = *p++;

        if (*ap != '-') {
            scan_file(ap);
            continue;
        }
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
                // output file name
            case 'o':
                if (++c >= argc)
                    error(2, "-o: argument missed");
                ld.ofilename = *p++;
                ld.ofilfnd++;
                continue;

                // 'use'
            case 'u':
                if (++c >= argc)
                    error(2, "-u: argument missed");
                enter_symbol(lookup_name(*p++));
                continue;

                // 'entry'
            case 'e':
                if (++c >= argc)
                    error(2, "-e: argument missed");
                enter_symbol(lookup_name(*p++));
                ld.entrypt = ld.lastsym;
                continue;

                // set data size
            case 'D':
                if (++c >= argc)
                    error(2, "-D: argument missed");
                num = W * atoi(*p++);
                if (ld.dsize > num)
                    error(2, "-D: too small");
                ld.dsize = num;
                continue;

                // base address of loading
            case 'T':
                ld.basaddr = strtoul(ap + i + 1, 0, 0);
                break;

                // library
            case 'l':
                save  = ap[--i];
                ap[i] = '-';
                scan_file(&ap[i]);
                ap[i] = save;
                break;

                // discard local symbols
            case 'x':
                ld.xflag++;
                continue;

                // discard locals starting with LOCSYM
            case 'X':
                ld.Xflag++;
                continue;

                // discard all except locals and globals
            case 'S':
                ld.Sflag++;
                continue;

                // put constants in data segment
            case 'C':
                ld.Cflag++;
                continue;

                // preserve rel. bits, don't define common
            case 'r':
                ld.rflag++;
                ld.arflag++;
                continue;

                // discard all symbols
            case 's':
                ld.sflag++;
                ld.xflag++;
                continue;

                // pure procedure
            case 'n':
                ld.nflag++;
                continue;

                // define common even with rflag
            case 'd':
                ld.dflag++;
                continue;

                // tracing
            case 't':
                ld.trace++;
                continue;

            case 'k':
                ld.alflag++;
                continue;

            default:
                error(2, "unknown flag");
            }
            break;
        }
    }
}
