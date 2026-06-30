//
// Assembler for BESM-6.
// Hash tables: instruction lookup and the symbol table.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

static int hash_instruction(const char *s)
{
    int h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HCMDSZ - 1);
}

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

static int hash_name(const char *s)
{
    int h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HASHSZ - 1);
}

static char *alloc_name(int len)
{
    int r;

    r = as.lastfree;
    if ((as.lastfree += len) > SPACESZ)
        fatal("out of memory");
    return as.space + r;
}

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

    // enter a new symbol into the table

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
