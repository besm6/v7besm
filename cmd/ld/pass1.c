//
// Linker for BESM-6 a.out objects.
// First pass: scan every object file and archive to collect the constant pool,
// segment sizes, and the global symbol table, and to parse the command line.
//
#include <stdlib.h>

#include "intern.h"

//
// Read the current file's constant pool and merge it into the program-wide pool
// (ld.constab), discarding duplicates so a constant used by many files is stored
// once.  Each constant is two half-words of value (h, h2) plus two half-words of
// relocation (hr, hr2); only non-relocatable constants (hr==hr2==0) can be shared,
// since a relocatable one means something different in each file.
//
// As it goes it fills ld.newindex[]: for each of this file's constants, the index
// of the pooled copy.  relocate_cursym() and relocate_halfword() use that map to
// repoint constant references.  Returns how many *new* pool entries were added.
//
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

        // A plain (non-relocatable) constant: look for an identical earlier one.
        if (!c->hr && !c->hr2)
            for (p = ld.constab; p < c; p++)
                if (!p->hr2 && c->h == p->h && c->h2 == p->h2 && !p->hr)
                    break;

        // p==c means no duplicate was found, so keep this new entry.
        if (p == c && ++c >= &ld.constab[NCONST])
            error(2, "constant table overflow");
        ld.newindex[ld.cindex++] = p - ld.constab;
    }
    ld.nconst = c - ld.constab;
    return ld.nconst - save;
}

//
// Pass-1 scan of one object file located at byte offset `loc`.  This is the heart
// of pass 1: it reads the header (accumulating segment sizes), merges the constant
// pool, and reads the symbol table to build the global symbol table.
//
// `libflg` is set when the file is an archive member: such a member is only kept
// if it actually defines a needed symbol (ndef > 0); otherwise everything it added
// is rolled back at the end and 0 is returned.  `nloc` is the byte size already
// reserved for this file's local symbols (e.g. its filename symbol).  Returns 1 if
// the file was incorporated, 0 if a library member was skipped.
//
int scan_object(long loc, int libflg, int nloc)
{
    struct nlist *sp;
    int savindex, savcindex;
    int ndef, nsymbol;

    read_header(loc);
    // RELFLG set means the file has no relocation records (it is fully linked),
    // so there is nothing to relocate against and it cannot be linked again.
    if (ld.filhdr.a_flag & RELFLG) {
        error(1, "not relocatable");
        return 0;
    }
    savcindex = ld.cindex;
    fseek(ld.reloc, loc + N_SYMOFF(ld.filhdr), 0);
    ld.coptsize[ld.nfile] = load_constants();
    ld.ctrel += ld.tsize / W;
    ld.cdrel += ld.dsize / W;
    ld.cbrel += ld.bsize / W;
    loc += HDRSZ + (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data) * 2;
    fseek(ld.text, loc, 0);
    ndef     = 0;
    savindex = ld.symindex;
    if (nloc)
        nsymbol = 1;
    else
        nsymbol = 0;

    // Read the file's symbol table one entry at a time (into ld.cursym) until the
    // terminating empty entry (symlen==1).
    for (;;) {
        int symlen = fgetsym(ld.text, &ld.cursym);
        int type;
        if (symlen == 0)
            error(2, "out of memory");
        if (symlen == 1)
            break;
        type = ld.cursym.n_type;

        // -S drops absolute and debug symbols.
        if (ld.Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_COMM)) {
            free(ld.cursym.n_name);
            continue;
        }

        // Local (non-external) symbol: it isn't shared, so just count the bytes
        // it will occupy in the output (unless we're discarding locals).
        if (!(type & N_EXT)) {
            if (!ld.sflag && !ld.xflag && (!ld.Xflag || ld.cursym.n_name[0] != LOCSYM)) {
                nsymbol++;
                nloc += symlen;
            }
            free(ld.cursym.n_name);
            continue;
        }

        // External symbol: relocate its value, then look it up in the global
        // table.  enter_symbol returns 1 if this is the first sighting (nothing
        // more to do); 0 if the name already exists and must be merged below.
        relocate_cursym();
        if (enter_symbol(lookup_symbol()))
            continue;
        free(ld.cursym.n_name);
        if (ld.cursym.n_type == N_EXT + N_UNDF)
            continue; // this occurrence is just another reference
        sp = ld.lastsym;

        // The existing entry is still unresolved (undefined or common).  Merge:
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM) {
            if (ld.cursym.n_type == N_EXT + N_COMM) {
                // New one is also common: keep the larger size requested.
                sp->n_type = ld.cursym.n_type;
                if (ld.cursym.n_value > sp->n_value)
                    sp->n_value = ld.cursym.n_value;
            } else if (sp->n_type == N_EXT + N_UNDF || ld.cursym.n_type == N_EXT + N_DATA ||
                       ld.cursym.n_type == N_EXT + N_BSS) {
                // New one is a real definition: adopt it (counts as a definition).
                ndef++;
                sp->n_type  = ld.cursym.n_type;
                sp->n_value = ld.cursym.n_value;
            }
        }
    }

    // Keep this file (always for a normal object; for a library member only if it
    // defined something): fold its segment sizes into the running totals.
    if (!libflg || ndef) {
        ld.csize = add_size(ld.csize, (long)W * ld.coptsize[ld.nfile++], "const segment overflow");
        ld.tsize = add_size(ld.tsize, ld.filhdr.a_text, "text segment overflow");
        ld.dsize = add_size(ld.dsize, ld.filhdr.a_data, "data segment overflow");
        ld.bsize = add_size(ld.bsize, ld.filhdr.a_bss, "bss segment overflow");
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
// Pass-1 handling of one command-line file argument `cp`.  open_input() decides
// what it is, and we scan it accordingly: a plain object is scanned outright; an
// ordinary archive is walked member by member; a randomized archive is resolved
// through its table of contents, repeatedly, until no more members are needed.
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
        // Step through every member; each header gives the offset of the next.
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
// The whole of pass 1: walk the command line once.  A non-option argument is a
// file to scan (scan_file); an option either sets a flag in `ld` or, for the ones
// that take a value (-o, -u, -e, -D, -T, -l), consumes/uses that value.  -u and -e
// pre-seed a symbol so it is treated as referenced (and, for -e, as the entry
// point) even if nothing else mentions it.
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

        // An option: ap[1..] are the flag letters (most are single letters; a few
        // take the following argv entry or the rest of this one).
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

            default:
                error(2, "unknown flag");
            }
            break;
        }
    }
}
