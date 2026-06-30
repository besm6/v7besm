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
    ctrel += torigin;
    cdrel += dorigin;
    cbrel += borigin;
    carel += aorigin;

    if (trace > 1)
        printf("ctrel=%lxh, cdrel=%lxh, cbrel=%lxh, carel=%lxh\n", ctrel, cdrel, cbrel, carel);
    /*
     * Re-read the symbol table, recording the numbering
     * of symbols for fixing external references.
     */
    lp    = local;
    symno = -1;
    loc += HDRSZ;
    fseek(text, loc + (filhdr.a_const + filhdr.a_text + filhdr.a_data) * 2, 0);
    for (;;) {
        symno++;
        count = fgetsym(text, &cursym);
        if (count == 0)
            error(2, "out of memory");
        if (count == 1)
            break;
        relocate_cursym();
        int type = cursym.n_type;
        if (Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_ACOMM)) {
            free(cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (!sflag && !xflag && (!Xflag || cursym.n_name[0] != LOCSYM))
                fputsym(&cursym, soutb);
            free(cursym.n_name);
            continue;
        }
        if (!(sp = *lookup_symbol()))
            error(2, "internal error: symbol not found");
        free(cursym.n_name);
        if (cursym.n_type == N_EXT + N_UNDF || cursym.n_type == N_EXT + N_COMM ||
            cursym.n_type == N_EXT + N_ACOMM) {
            if (lp >= &local[NSYMPR])
                error(2, "local symbol table overflow");
            lp->locindex    = symno;
            lp++->locsymbol = sp;
            continue;
        }
        if (cursym.n_type != sp->n_type || cursym.n_value != sp->n_value) {
            printf("%s: ", cursym.n_name);
            error(1, "name redefined");
        }
    }

    count = loc + filhdr.a_const + filhdr.a_text + filhdr.a_data;

    if (trace > 1)
        printf("** CONST **\n");
    relocate_constants(lp);

    if (trace > 1)
        printf("** TEXT **\n");
    fseek(text, loc + filhdr.a_const, 0);
    fseek(reloc, count + filhdr.a_const, 0);
    relocate_segment(lp, toutb, troutb, filhdr.a_text);

    if (trace > 1)
        printf("** DATA **\n");
    fseek(text, loc + filhdr.a_const + filhdr.a_text, 0);
    fseek(reloc, count + filhdr.a_const + filhdr.a_text, 0);
    relocate_segment(lp, doutb, droutb, filhdr.a_data);

    nconst += coptsize[nfile];
    cindex += filhdr.a_const / W;
    corigin += coptsize[nfile];
    torigin += filhdr.a_text / W;
    dorigin += filhdr.a_data / W;
    borigin += filhdr.a_bss / W;
    aorigin += filhdr.a_abss / W;
    nfile++;
}

void relocate_file(char *acp)
{
    if (open_input(acp) == 0) {
        if (trace)
            printf("%s:\n", acp);
        make_file_symbol(acp, 1);
        relocate_object(0L);
    } else {
        /* scan archive members referenced */
        const char *arname = acp;
        long *lp;

        for (lp = libp; *lp != -1; lp++) {
            fseek(text, *lp, 0);
            fgetarhdr(text, &archdr);
            acp = malloc(sizeof(archdr.ar_name) + 1);
            if (!acp)
                error(2, "out of memory");
            // cppcheck-suppress nullPointerOutOfMemory
            strncpy(acp, archdr.ar_name, sizeof(archdr.ar_name));
            acp[sizeof(archdr.ar_name)] = '\0';
            if (trace)
                printf("%s(%s):\n", arname, acp);
            make_file_symbol(acp, 1);
            free(acp);
            relocate_object(*lp + ARHDRSZ);
        }
        libp = ++lp;
    }
    fclose(text);
    fclose(reloc);
}

void pass2(int argc, char **argv)
{
    int c, i;
    long dnum;
    char *ap, **p;

    p      = argv + 1;
    libp   = liblist;
    cindex = 0;
    nconst = 0;
    nfile  = 0;
    for (c = 1; c < argc; c++) {
        ap = *p++;
        if (*ap != '-') {
            relocate_file(ap);
            continue;
        }
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
            case 'D':
                /*
                 * I think it should actually be like this:
                 *              for (dnum=atoi(*p); dorigin<dnum; dorigin++) {
                 */
                for (dnum = atoi(*p); dnum > 0; --dnum) {
                    fputw(0, doutb);
                    if (rflag) {
                        fputw(0, droutb);
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
