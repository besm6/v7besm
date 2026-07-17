//
// Linker for BESM-6 a.out objects.
// Symbol table: hashing, lookup/insertion, symbol relocation, and the file
// symbols emitted into the output.
//
#include <stdlib.h>
#include <string.h>

#include "intern.h"

//
// Adjust the value of the symbol just read (ld.cursym) so it refers to the right
// place once this file's segments are merged with everyone else's.  Which
// adjustment depends on the symbol's segment type:
//   - const: identical literals were merged away, so look up where this file's
//     const word ended up via the newindex map.
//   - text/data/bss: shift by this file's relocation bias for that segment.
//   - undefined / common: nothing to do yet (resolved later).
//   - anything else external: treat as a plain absolute value.
//
void relocate_cursym(void)
{
    int i;

    switch (ld.cursym.n_type) {
    case N_CONST:
    case N_EXT + N_CONST:
        // Where this file's const word ended up after merging.  ld.cindex is the
        // file's base within newindex[] in both passes, so the symbol's word
        // offset indexes straight off it.  cbasaddr is the segment's final origin,
        // and is still 0 during pass 1 - which is what assign_addresses() expects,
        // since it adds corigin itself.  By pass 2 it is set, so the value
        // recomputed here matches the one pass 1 settled on (the same trick
        // ctrel/cdrel/cbrel play in relocate_object()).
        //
        // The map only covers the words this file contributed, so the symbol has
        // to name one of them.  A const symbol pointing outside the segment has no
        // word to follow and cannot be relocated at all: identical literals are
        // merged away individually, so the words this file brought are scattered
        // through the pool, and there is nothing to extrapolate past either edge.
        // "sym = . - 8" at the top of a .const is the usual way to land here -
        // anchor such a symbol inside the segment instead.
        i = ld.cindex + ld.cursym.n_value - HDRSZ / W;
        if (i < ld.cindex || i >= ld.cindex + (int)(ld.filhdr.a_const / W))
            error(2, "symbol '%s': const value 0%lo outside the file's const segment",
                  ld.cursym.n_name, (long)ld.cursym.n_value);
        ld.cursym.n_value = ld.newindex[i] + ld.cbasaddr;
        return;

    case N_TEXT:
    case N_EXT + N_TEXT:
        ld.cursym.n_value += ld.ctrel;
        return;

    case N_DATA:
    case N_EXT + N_DATA:
        ld.cursym.n_value += ld.cdrel;
        return;

    case N_BSS:
    case N_EXT + N_BSS:
        ld.cursym.n_value += ld.cbrel;
        return;

    case N_EXT + N_UNDF:
    case N_EXT + N_COMM:
        return;
    }
    if (ld.cursym.n_type & N_EXT)
        ld.cursym.n_type = N_EXT + N_ABS;
}

//
// Insert ld.cursym into the symbol table, given the hash slot `hp` that
// lookup_symbol() found for it.  If the slot is empty (a brand-new name), copy
// the symbol into the next free symtab entry and return 1.  If the name is
// already present, just remember it in ld.lastsym and return 0 (the caller then
// merges the two definitions).
//
int enter_symbol(struct nlist **hp)
{
    struct nlist *sp;

    if (!*hp) {
        if (ld.symindex >= NSYM)
            error(2, "symbol table overflow");
        ld.symhash[ld.symindex] = hp;
        *hp = ld.lastsym = sp = &ld.symtab[ld.symindex++];
        sp->n_len             = ld.cursym.n_len;
        sp->n_name            = ld.cursym.n_name;
        sp->n_type            = ld.cursym.n_type;
        sp->n_value           = ld.cursym.n_value;
        return 1;
    } else {
        ld.lastsym = *hp;
        return 0;
    }
}

//
// Find the hash-table slot for the current symbol's name (ld.cursym.n_name).
// Returns a pointer to that slot: it points to the matching symbol if the name
// is already known, or to an empty slot (*slot == 0) where it should be added.
//
// The table uses open addressing: hash the name into a starting bucket, then
// step forward one bucket at a time past any names that don't match (wrapping
// around at the end) until we hit either our name or an empty slot.  The first
// two buckets are reserved, hence "% NSYM + 2".
//
struct nlist **lookup_symbol(void)
{
    int i;
    char *cp;
    struct nlist **hp;

    // Fold the name's characters into a hash value i.
    i = 0;
    for (cp = ld.cursym.n_name; *cp; i = (i << 1) + *cp++)
        ;
    for (hp = &ld.hshtab[(i & 077777) % NSYM + 2]; *hp != 0;) {
        const char *cp1 = (*hp)->n_name;
        int clash       = 0;
        for (cp = ld.cursym.n_name; *cp;)
            if (*cp++ != *cp1++) {
                clash = 1;
                break;
            }
        if (clash) {
            // Occupied by a different name: try the next bucket, wrapping.
            if (++hp >= &ld.hshtab[NSYM + 2])
                hp = ld.hshtab;
        } else
            break; // found our name, or an empty slot
    }
    return hp;
}

//
// Convenience wrapper: look up a plain C string `s` by stuffing it into
// ld.cursym as an undefined external and calling lookup_symbol().  Used to find
// or create the linker's own named symbols (e.g. "etext", or -u/-e names).
//
struct nlist **lookup_name(char *s)
{
    ld.cursym.n_len   = strlen(s) + 1;
    ld.cursym.n_name  = s;
    ld.cursym.n_type  = N_EXT + N_UNDF;
    ld.cursym.n_value = 0;
    return lookup_symbol();
}

//
// Give the symbol `sp` a definition: set its type and value.  Used for the
// linker's built-in boundary symbols (etext, ...).  It must currently be an
// undefined external; defining an already-defined name is an error.  A NULL `sp`
// (the name was never referenced) is silently ignored.
//
void define_symbol(struct nlist *sp, long val, int type)
{
    if (sp == 0)
        return;
    if (sp->n_type != N_EXT + N_UNDF) {
        error(1, "name '%s' redefined", sp->n_name);
        return;
    }
    sp->n_type  = type;
    sp->n_value = val;
}

//
// Map a file-local symbol number `sn` to the global symbol it stands for.
// During pass 2 a file's external references point at its local symbols by
// number; the `local` array (entries 0..lp-1) records the number->symbol pairing
// built earlier for this file.  A miss means the object is malformed.
//
struct nlist *lookup_local(const struct local *lp, int sn)
{
    const struct local *clp;

    for (clp = ld.local; clp < lp; clp++)
        if (clp->locindex == sn)
            return clp->locsymbol;
    if (ld.trace) {
        fprintf(stderr, "*** %d ***\n", sn);
        for (clp = ld.local; clp < lp; clp++)
            fprintf(stderr, "%ld, ", clp->locindex);
        fprintf(stderr, "\n");
    }
    error(2, "bad symbol reference");
    // NOTREACHED
    return 0;
}

//
// Produce a debugger "file name" symbol (type N_FN) for input file `s`, so the
// symbol table records which file the following symbols came from.  Any
// directory prefix is stripped, keeping just the base name.
//
// Two modes: with wflag==0 it only returns how many bytes such a symbol *would*
// occupy (so pass 1 can budget for it); with wflag!=0 it actually writes the
// symbol to the output.  Either way it returns that byte size, or 0 when symbols
// are being discarded (-s/-x).
//
int make_file_symbol(char *s, int wflag)
{
    char *p;

    if (ld.sflag || ld.xflag)
        return 0;
    for (p = s; *p;) // strip everything up to the last '/'
        if (*p++ == '/')
            s = p;
    if (!wflag)
        return p - s + 6;
    ld.cursym.n_len  = p - s;
    ld.cursym.n_name = malloc(ld.cursym.n_len + 1);
    if (!ld.cursym.n_name)
        error(2, "out of memory");
    for (p = ld.cursym.n_name; *s; p++, s++)
        *p = *s;
    ld.cursym.n_type  = N_FN;
    ld.cursym.n_value = ld.torigin;
    fputsym(&ld.cursym, ld.soutb);
    free(ld.cursym.n_name);
    return ld.cursym.n_len + 6;
}
