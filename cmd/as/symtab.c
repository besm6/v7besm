//
// Assembler for BESM-6.
// Hash tables: looking up machine-instruction mnemonics and the program's
// symbol names.  All three tables here (instructions, symbols, constants - the
// last lives in pass1.c) use the same scheme: a hash gives a starting slot,
// and on a collision we step to the PREVIOUS slot, wrapping around, until we
// find the entry or an empty slot.  Empty slots hold -1.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

//
// Hash a mnemonic string into the instruction table's index range.  The little
// "h += h + c" loop mixes the characters; SUPERHASH then folds and masks the
// result down to a valid slot.
//
static int hash_instruction(const char *s)
{
    int h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HCMDSZ - 1);
}

//
// Build the hash tables once at start-up: clear the constant-pool and symbol
// buckets to empty, and load every entry of the machine-instruction table[]
// into its instruction-hash bucket (probing to the previous slot on a clash).
//
void init_hash_tables(void)
{
    int i;
    const struct table *p;

    for (i = 0; i < HCONSZ; i++)
        as.hashconst[i] = -1;
    for (i = 0; i < HCMDSZ; i++)
        as.hashctab[i] = -1;
    for (p = table; p->name; p++) {
        int h = hash_instruction(p->name);

        while (as.hashctab[h] != -1)
            if (--h < 0)
                h += HCMDSZ;
        as.hashctab[h] = p - table;
    }
    for (i = 0; i < HASHSZ; i++)
        as.hashtab[i] = -1;
}

//
// Recognise the directive name in as.name (e.g. ".text") and return its code,
// or -1 if it is not a directive.  Switching on the second character (the one
// after the '.') narrows it to at most a couple of string compares.
//
int lookup_directive(void)
{
    switch (as.name[1]) {
    case 'a':
        if (!strcmp(".ascii", as.name))
            return ASCII;
        if (!strcmp(".acomm", as.name))
            return ACOMM;
        break;
    case 'b':
        if (!strcmp(".bss", as.name))
            return BSS;
        break;
    case 'c':
        if (!strcmp(".comm", as.name))
            return COMM;
        break;
    case 'd':
        if (!strcmp(".data", as.name))
            return DATA;
        break;
    case 'e':
        if (!strcmp(".equ", as.name))
            return EQU;
        break;
    case 'g':
        if (!strcmp(".globl", as.name))
            return GLOBL;
        break;
    case 'h':
        if (!strcmp(".half", as.name))
            return HALF;
        break;
    case 's':
        if (!strcmp(".strng", as.name))
            return STRNG;
        break;
    case 't':
        if (!strcmp(".text", as.name))
            return TEXT;
        break;
    case 'w':
        if (!strcmp(".word", as.name))
            return WORD;
        break;
    }
    return -1;
}

//
// Look up the mnemonic in as.name in the instruction table.  Return its index
// in table[], or -1 if it is not a known instruction (in which case the lexer
// treats the word as an ordinary symbol name).
//
int lookup_instruction(void)
{
    int i, h;

    h = hash_instruction(as.name);
    while ((i = as.hashctab[h]) != -1) {
        if (!strcmp(table[i].name, as.name))
            return i;
        if (--h < 0)
            h += HCMDSZ;
    }
    return -1;
}

//
// Hash a symbol name into the symbol table's index range (same mixing as
// hash_instruction, but masked to the larger symbol-table size).
//
static int hash_name(const char *s)
{
    int h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HASHSZ - 1);
}

//
// Allocate `len` bytes of name storage from the arena (space[]).  This is a
// simple bump allocator - hand out the next free bytes and advance the cursor;
// there is no freeing - that runs out of room only on an enormous program.
//
static char *alloc_name(int len)
{
    int r;

    r = as.lastfree;
    if ((as.lastfree += len) > SPACESZ)
        fatal("out of memory");
    return as.space + r;
}

//
// Find the symbol named in as.name, or create it if new, and return its index
// in the symbol table.  A freshly created symbol starts out undefined (type 0,
// value 0); the caller fills it in (as a label, .equ, .comm, .globl, ...).
//
int lookup_name(void)
{
    int i, h;

    h = hash_name(as.name);
    while ((i = as.hashtab[h]) != -1) {
        if (!strcmp(as.stab[i].n_name, as.name))
            return i;
        if (--h < 0)
            h += HASHSZ;
    }

    // Not found: enter a new symbol, copying its name into the arena.

    i = as.stabfree++;
    if (i >= STSIZE)
        fatal("symbol table overflow");
    else {
        as.stab[i].n_len  = strlen(as.name);
        as.stab[i].n_name = alloc_name(1 + as.stab[i].n_len);
        strcpy(as.stab[i].n_name, as.name);
        as.stab[i].n_value = 0;
        as.stab[i].n_type  = 0;
        as.hashtab[h]      = i;
        return i;
    }
}
