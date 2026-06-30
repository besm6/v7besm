//
// Linker for BESM-6 a.out objects.
// Input file and library/archive handling: opening files, stepping through
// archive members, and resolving randomized-archive table-of-contents entries.
//
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "intern.h"

int getfile(char *cp)
{
    int c;
    struct stat x;

    text    = 0;
    filname = cp;
    if (cp[0] == '-' && cp[1] == 'l') {
        if (cp[2] == '\0')
            cp = "-la";
        filname = libname;
        for (c = 0; cp[c + 2]; c++)
            filname[c + LNAMLEN] = cp[c + 2];
        filname[c + LNAMLEN]     = '.';
        filname[c + LNAMLEN + 1] = 'a';
        filname[c + LNAMLEN + 2] = '\0';
        text                     = fopen(filname, "r");
        if (!text)
            filname += 4;
    }
    if (!text && !(text = fopen(filname, "r")))
        error(2, "cannot open");
    reloc = fopen(filname, "r");
    if (!reloc)
        error(2, "cannot open");
    if (!fgetint(text, &c))
        error(1, "unexpected EOF");
    if (c != ARMAG)
        return 0; /* regular file */
    if (!fgetarhdr(text, &archdr))
        return 1; /* regular archive */
    if (strncmp(archdr.ar_name, SYMDEF, sizeof(archdr.ar_name)))
        return 1; /* regular archive */
    fstat(fileno(text), &x);
    if (x.st_mtime > archdr.ar_date + 2)
        return 3; /* out of date archive */
    return 2;     /* randomized archive */
}

void checklibp(void)
{
    if (libp >= &liblist[LLSIZE])
        error(2, "library table overflow");
}

int step(long nloc)
{
    char *cp;

    fseek(text, nloc, 0);
    if (!fgetarhdr(text, &archdr)) {
        *libp++ = -1;
        checklibp();
        return 0;
    }
    cp = malloc(sizeof(archdr.ar_name) + 1);
    if (!cp)
        error(2, "out of memory");
    // cppcheck-suppress nullPointerOutOfMemory
    strncpy(cp, archdr.ar_name, sizeof(archdr.ar_name));
    cp[sizeof(archdr.ar_name)] = '\0';
    if (load1(nloc + ARHDRSZ, 1, mkfsym(cp, 0)))
        *libp++ = nloc;
    free(cp);
    checklibp();
    return 1;
}

void getrantab(void)
{
    struct ranlib *p;

    for (p = rantab; p < rantab + RANTABSZ; ++p) {
        int n = fgetran(text, p);
        if (n < 0)
            error(2, "out of memory");
        if (n == 0) {
            tnum = p - rantab;
            return;
        }
    }
    error(2, "ranlib buffer overflow");
}

int ldrand(void)
{
    struct ranlib *p;
    const long *oldp = libp;

    for (p = rantab; p < rantab + tnum; ++p) {
        struct nlist **pp = slookup(p->ran_name);
        if (!*pp)
            continue;
        if ((*pp)->n_type == N_EXT + N_UNDF)
            step(p->ran_off);
    }
    return (oldp != libp);
}

void freerantab(void)
{
    struct ranlib *p;

    for (p = rantab; p < rantab + tnum; ++p)
        free(p->ran_name);
}
