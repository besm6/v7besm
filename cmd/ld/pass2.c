//
// Linker for BESM-6 a.out objects.
// Second pass: re-read each object file, renumber external references, and
// write the relocated constant, text, and data segments to the output buffers.
//
#include <stdlib.h>
#include <string.h>

#include "intern.h"

void relocate_object(long loc)
{
    struct nlist *sp;
    struct local *lp;
    int symno;
    long count;

    read_header(loc);
    ld.ctrel += ld.torigin;
    ld.cdrel += ld.dorigin;
    ld.cbrel += ld.borigin;
    ld.carel += ld.aorigin;

    if (ld.trace > 1)
        printf("ctrel=%lxh, cdrel=%lxh, cbrel=%lxh, carel=%lxh\n", ld.ctrel, ld.cdrel, ld.cbrel,
               ld.carel);
    //
    // Re-read the symbol table, recording the numbering
    // of symbols for fixing external references.
    //
    lp    = ld.local;
    symno = -1;
    loc += HDRSZ;
    fseek(ld.text, loc + (ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data) * 2, 0);
    for (;;) {
        symno++;
        count = fgetsym(ld.text, &ld.cursym);
        if (count == 0)
            error(2, "out of memory");
        if (count == 1)
            break;
        relocate_cursym();
        int type = ld.cursym.n_type;
        if (ld.Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_ACOMM)) {
            free(ld.cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (!ld.sflag && !ld.xflag && (!ld.Xflag || ld.cursym.n_name[0] != LOCSYM))
                fputsym(&ld.cursym, ld.soutb);
            free(ld.cursym.n_name);
            continue;
        }
        if (!(sp = *lookup_symbol()))
            error(2, "internal error: symbol not found");
        free(ld.cursym.n_name);
        if (ld.cursym.n_type == N_EXT + N_UNDF || ld.cursym.n_type == N_EXT + N_COMM ||
            ld.cursym.n_type == N_EXT + N_ACOMM) {
            if (lp >= &ld.local[NSYMPR])
                error(2, "local symbol table overflow");
            lp->locindex    = symno;
            lp++->locsymbol = sp;
            continue;
        }
        if (ld.cursym.n_type != sp->n_type || ld.cursym.n_value != sp->n_value) {
            printf("%s: ", ld.cursym.n_name);
            error(1, "name redefined");
        }
    }

    count = loc + ld.filhdr.a_const + ld.filhdr.a_text + ld.filhdr.a_data;

    if (ld.trace > 1)
        printf("** CONST **\n");
    relocate_constants(lp);

    if (ld.trace > 1)
        printf("** TEXT **\n");
    fseek(ld.text, loc + ld.filhdr.a_const, 0);
    fseek(ld.reloc, count + ld.filhdr.a_const, 0);
    relocate_segment(lp, ld.toutb, ld.troutb, ld.filhdr.a_text);

    if (ld.trace > 1)
        printf("** DATA **\n");
    fseek(ld.text, loc + ld.filhdr.a_const + ld.filhdr.a_text, 0);
    fseek(ld.reloc, count + ld.filhdr.a_const + ld.filhdr.a_text, 0);
    relocate_segment(lp, ld.doutb, ld.droutb, ld.filhdr.a_data);

    ld.nconst += ld.coptsize[ld.nfile];
    ld.cindex += ld.filhdr.a_const / W;
    ld.corigin += ld.coptsize[ld.nfile];
    ld.torigin += ld.filhdr.a_text / W;
    ld.dorigin += ld.filhdr.a_data / W;
    ld.borigin += ld.filhdr.a_bss / W;
    ld.aorigin += ld.filhdr.a_abss / W;
    ld.nfile++;
}

void relocate_file(char *acp)
{
    if (open_input(acp) == 0) {
        if (ld.trace)
            printf("%s:\n", acp);
        make_file_symbol(acp, 1);
        relocate_object(0L);
    } else {
        // scan archive members referenced
        const char *arname = acp;
        long *lp;

        for (lp = ld.libp; *lp != -1; lp++) {
            fseek(ld.text, *lp, 0);
            fgetarhdr(ld.text, &ld.archdr);
            acp = malloc(sizeof(ld.archdr.ar_name) + 1);
            if (!acp)
                error(2, "out of memory");
            // cppcheck-suppress nullPointerOutOfMemory
            strncpy(acp, ld.archdr.ar_name, sizeof(ld.archdr.ar_name));
            acp[sizeof(ld.archdr.ar_name)] = '\0';
            if (ld.trace)
                printf("%s(%s):\n", arname, acp);
            make_file_symbol(acp, 1);
            free(acp);
            relocate_object(*lp + ARHDRSZ);
        }
        ld.libp = ++lp;
    }
    fclose(ld.text);
    fclose(ld.reloc);
}

void pass2(int argc, char **argv)
{
    int c, i;
    long dnum;
    char *ap, **p;

    p         = argv + 1;
    ld.libp   = ld.liblist;
    ld.cindex = 0;
    ld.nconst = 0;
    ld.nfile  = 0;
    for (c = 1; c < argc; c++) {
        ap = *p++;
        if (*ap != '-') {
            relocate_file(ap);
            continue;
        }
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
            case 'D':
                //
                // I think it should actually be like this:
                //              for (dnum=atoi(*p); dorigin<dnum; dorigin++) {
                //
                for (dnum = atoi(*p); dnum > 0; --dnum) {
                    fputw(0, ld.doutb);
                    if (ld.rflag) {
                        fputw(0, ld.droutb);
                    }
                }
            case 'u':
            case 'e':
            case 'o':
            case 'v':
                ++c;
                ++p;

            default:
                continue;

            case 'l':
                ap[--i] = '-';
                relocate_file(&ap[i]);
                break;
            }
            break;
        }
    }
}
