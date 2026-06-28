//
// Assembler for BESM-6.
// Hash tables: instruction lookup and the symbol table.
//
#include <stdio.h>
#include <string.h>

#include "as.h"

static int chash(const char *s)
{
    short h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HCMDSZ - 1);
}

void hashinit(void)
{
    short i;
    const struct table *p;

    for (i = 0; i < HCONSZ; i++)
        hashconst[i] = -1;
    for (i = 0; i < HCMDSZ; i++)
        hashctab[i] = -1;
    for (p = table; p->name; p++) {
        short h = chash(p->name);

        while (hashctab[h] != -1)
            if (--h < 0)
                h += HCMDSZ;
        hashctab[h] = p - table;
    }
    for (i = 0; i < HASHSZ; i++)
        hashtab[i] = -1;
}

int lookacmd(void)
{
    switch (name[1]) {
    case 'a':
        if (!strcmp(".ascii", name))
            return ASCII;
        if (!strcmp(".acomm", name))
            return ACOMM;
        break;
    case 'b':
        if (!strcmp(".bss", name))
            return BSS;
        break;
    case 'c':
        if (!strcmp(".comm", name))
            return COMM;
        break;
    case 'd':
        if (!strcmp(".data", name))
            return DATA;
        break;
    case 'e':
        if (!strcmp(".equ", name))
            return EQU;
        break;
    case 'g':
        if (!strcmp(".globl", name))
            return GLOBL;
        break;
    case 'h':
        if (!strcmp(".half", name))
            return HALF;
        break;
    case 's':
        if (!strcmp(".strng", name))
            return STRNG;
        break;
    case 't':
        if (!strcmp(".text", name))
            return TEXT;
        break;
    case 'w':
        if (!strcmp(".word", name))
            return WORD;
        break;
    }
    return -1;
}

int lookcmd(void)
{
    short i, h;

    h = chash(name);
    while ((i = hashctab[h]) != -1) {
        if (!strcmp(table[i].name, name))
            return i;
        if (--h < 0)
            h += HCMDSZ;
    }
    return -1;
}

static int hash(const char *s)
{
    short h, c;

    h = 12345;
    while ((c = *s++))
        h += h + c;
    return SUPERHASH(h, HASHSZ - 1);
}

static char *alloc(int len)
{
    short r;

    r = lastfree;
    if ((lastfree += len) > SPACESZ)
        uerror("out of memory");
    return space + r;
}

int lookname(void)
{
    short i, h;

    h = hash(name);
    while ((i = hashtab[h]) != -1) {
        if (!strcmp(stab[i].n_name, name))
            return i;
        if (--h < 0)
            h += HASHSZ;
    }

    // enter a new symbol into the table

    i = stabfree++;
    if (i >= STSIZE)
        uerror("symbol table overflow");
    else {
        stab[i].n_len  = strlen(name);
        stab[i].n_name = alloc(1 + stab[i].n_len);
        strcpy(stab[i].n_name, name);
        stab[i].n_value = 0;
        stab[i].n_type  = 0;
        hashtab[h]      = i;
        return i;
    }
}
