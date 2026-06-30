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

int open_input(char *cp)
{
    int c;
    struct stat x;

    ld.text    = 0;
    ld.filname = cp;
    if (cp[0] == '-' && cp[1] == 'l') {
        if (cp[2] == '\0')
            cp = "-la";
        ld.filname = ld.libname;
        for (c = 0; cp[c + 2]; c++)
            ld.filname[c + LNAMLEN] = cp[c + 2];
        ld.filname[c + LNAMLEN]     = '.';
        ld.filname[c + LNAMLEN + 1] = 'a';
        ld.filname[c + LNAMLEN + 2] = '\0';
        ld.text                     = fopen(ld.filname, "r");
        if (!ld.text)
            ld.filname += 4;
    }
    if (!ld.text && !(ld.text = fopen(ld.filname, "r")))
        error(2, "cannot open");
    ld.reloc = fopen(ld.filname, "r");
    if (!ld.reloc)
        error(2, "cannot open");
    if (!fgetint(ld.text, &c))
        error(1, "unexpected EOF");
    if (c != ARMAG)
        return 0; // regular file
    if (!fgetarhdr(ld.text, &ld.archdr))
        return 1; // regular archive
    if (strncmp(ld.archdr.ar_name, SYMDEF, sizeof(ld.archdr.ar_name)))
        return 1; // regular archive
    fstat(fileno(ld.text), &x);
    if (x.st_mtime > ld.archdr.ar_date + 2)
        return 3; // out of date archive
    return 2;     // randomized archive
}

void check_liblist(void)
{
    if (ld.libp >= &ld.liblist[LLSIZE])
        error(2, "library table overflow");
}

int scan_member(long nloc)
{
    char *cp;

    fseek(ld.text, nloc, 0);
    if (!fgetarhdr(ld.text, &ld.archdr)) {
        *ld.libp++ = -1;
        check_liblist();
        return 0;
    }
    cp = malloc(sizeof(ld.archdr.ar_name) + 1);
    if (!cp)
        error(2, "out of memory");
    // cppcheck-suppress nullPointerOutOfMemory
    strncpy(cp, ld.archdr.ar_name, sizeof(ld.archdr.ar_name));
    cp[sizeof(ld.archdr.ar_name)] = '\0';
    if (scan_object(nloc + ARHDRSZ, 1, make_file_symbol(cp, 0)))
        *ld.libp++ = nloc;
    free(cp);
    check_liblist();
    return 1;
}

void read_ranlib(void)
{
    struct ranlib *p;

    for (p = ld.rantab; p < ld.rantab + RANTABSZ; ++p) {
        int n = fgetran(ld.text, p);
        if (n < 0)
            error(2, "out of memory");
        if (n == 0) {
            ld.tnum = p - ld.rantab;
            return;
        }
    }
    error(2, "ranlib buffer overflow");
}

int load_ranlib_members(void)
{
    struct ranlib *p;
    const long *oldp = ld.libp;

    for (p = ld.rantab; p < ld.rantab + ld.tnum; ++p) {
        struct nlist **pp = lookup_name(p->ran_name);
        if (!*pp)
            continue;
        if ((*pp)->n_type == N_EXT + N_UNDF)
            scan_member(p->ran_off);
    }
    return (oldp != ld.libp);
}

void free_ranlib(void)
{
    struct ranlib *p;

    for (p = ld.rantab; p < ld.rantab + ld.tnum; ++p)
        free(p->ran_name);
}
