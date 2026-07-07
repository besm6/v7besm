//
// Linker for BESM-6 a.out objects.
// Second pass: re-read each object file, renumber external references, and
// write the relocated constant, text, and data segments to the output buffers.
//
#include <stdlib.h>
#include <string.h>

#include "intern.h"

//
// Pass-2 processing of one object file at byte offset `loc`.  By now every symbol
// has a final address, so this routine emits the file's actual contents: it walks
// the file's symbol table to build the local number->global symbol map (used to
// resolve external references), then copies the const, text and data segments to
// the output buffers, relocating every address field on the way through.  At the
// end it advances all the running origins past this file's contribution.
//
void relocate_object(long loc)
{
    struct nlist *sp;
    struct local *lp;
    int symno;
    long count;

    // Turn this file's relocation biases (relative offsets) into absolute ones by
    // adding the final base address chosen for each segment.
    read_header(loc);
    ld.ctrel += ld.torigin;
    ld.cdrel += ld.dorigin;
    ld.cbrel += ld.borigin;

    if (ld.trace > 1)
        printf("ctrel=%lxh, cdrel=%lxh, cbrel=%lxh\n", ld.ctrel, ld.cdrel, ld.cbrel);
    //
    // Re-read the symbol table, recording, for each external reference, which
    // global symbol the file's local symbol number maps to (in ld.local[]).
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
        if (ld.Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_COMM)) {
            free(ld.cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (!ld.sflag && !ld.xflag && (!ld.Xflag || ld.cursym.n_name[0] != LOCSYM))
                fputsym(&ld.cursym, ld.soutb);
            free(ld.cursym.n_name);
            continue;
        }

        // Find the global entry for this external name (it must exist now).
        if (!(sp = *lookup_symbol()))
            error(2, "internal error: symbol not found");
        free(ld.cursym.n_name);

        // Still unresolved (undefined/common): remember symno -> sp so that
        // references in this file's code can be steered to the global symbol.
        if (ld.cursym.n_type == N_EXT + N_UNDF || ld.cursym.n_type == N_EXT + N_COMM) {
            if (lp >= &ld.local[NSYMPR])
                error(2, "local symbol table overflow");
            lp->locindex    = symno;
            lp++->locsymbol = sp;
            continue;
        }

        // Defined here: it had better agree with the definition pass 1 chose.
        if (ld.cursym.n_type != sp->n_type || ld.cursym.n_value != sp->n_value) {
            printf("%s: ", ld.cursym.n_name);
            error(1, "name redefined");
        }
    }

    // Relocate and emit the three stored segments.  ld.text is positioned at the
    // segment data, ld.reloc at the matching relocation records (which sit one
    // const+text+data block further on in the file).
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

    // Advance every running counter past this file's contribution so the next
    // file's contents land immediately after it.
    ld.nconst += ld.coptsize[ld.nfile];
    ld.cindex += ld.filhdr.a_const / W;
    ld.corigin += ld.coptsize[ld.nfile];
    ld.torigin += ld.filhdr.a_text / W;
    ld.dorigin += ld.filhdr.a_data / W;
    ld.borigin += ld.filhdr.a_bss / W;
    ld.nfile++;
}

//
// Pass-2 handling of one command-line file argument.  A plain object is relocated
// directly; for an archive we revisit exactly the members pass 1 decided to keep
// (their offsets were recorded in ld.liblist), in the same order, and relocate
// each.  Emits a filename symbol before each so the symbol table stays annotated.
//
void relocate_file(char *acp)
{
    if (open_input(acp) == 0) {
        if (ld.trace)
            printf("%s:\n", acp);
        make_file_symbol(acp, 1);
        relocate_object(0L);
    } else {
        // Replay the archive members recorded during pass 1 (up to the -1 mark).
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
    free(ld.filname_alloc);
    ld.filname_alloc = 0;
    ld.filname       = 0;
}

//
// The whole of pass 2: walk the command line a second time, in the same order, so
// files are emitted in the layout pass 1 planned.  Non-option arguments are
// relocated (relocate_file); options are mostly skipped here, but the ones that
// took a value (-u/-e/-o/-v) must still step over that value, and -D emits the
// extra zero-filled data words it reserved, and -l re-triggers an archive.
//
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
            case 'D': // emit the reserved data words as zeros (note: falls through)
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
