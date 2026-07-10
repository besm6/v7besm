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

//
// Open an input argument `cp` and report what kind of thing it is.  Opens two
// stdio handles on it: ld.text for the segment/symbol data and ld.reloc for the
// relocation records (the two passes read from different positions, so each gets
// its own handle).  A "-lfoo" argument is expanded to "libfoo.a" and looked up
// in each -L search directory in turn (ld.libdir), falling back to "libfoo.a" in
// the current directory if the search path is empty or nothing matched.
//
// Return value classifies the file so the caller knows how to read it:
//      0 - a plain object file
//      1 - an ordinary archive (must scan members one by one)
//      2 - a randomized archive (has a "__.SYMDEF" table of contents)
//      3 - a randomized archive whose contents are out of date (warn, scan it)
//
int open_input(char *cp)
{
    int c;
    struct stat x;

    ld.text          = 0;
    ld.filname       = cp;
    ld.filname_alloc = 0;
    if (cp[0] == '-' && cp[1] == 'l') {
        // Bare "-l" means "-la"; otherwise the name is whatever follows "-l".
        const char *name = (cp[2] == '\0') ? "a" : cp + 2;
        int i;

        // Search each -L directory in turn for "<dir>/lib<name>.a".
        for (i = 0; !ld.text && i < ld.nlibdir; i++) {
            const char *dir = ld.libdir[i];
            size_t sz       = strlen(dir) + strlen(name) + sizeof("/lib.a");
            char *p         = malloc(sz);
            if (!p) {
                error(2, "out of memory");
            } else {
                // cppcheck-suppress nullPointerOutOfMemory
                snprintf(p, sz, "%s/lib%s.a", dir, name);
                ld.text = fopen(p, "r");
                if (ld.text) {
                    ld.filname       = p;
                    ld.filname_alloc = p;
                } else {
                    free(p);
                }
            }
        }

        // Nothing matched (or the search path is empty): try "lib<name>.a" in
        // the current directory.  The open below reports failure if it is absent.
        if (!ld.text) {
            size_t sz = strlen(name) + sizeof("lib.a");
            char *p   = malloc(sz);
            if (!p) {
                error(2, "out of memory");
            } else {
                // cppcheck-suppress nullPointerOutOfMemory
                snprintf(p, sz, "lib%s.a", name);
                ld.filname       = p;
                ld.filname_alloc = p;
            }
        }
    }
    if (!ld.text && !(ld.text = fopen(ld.filname, "r")))
        error(2, "cannot open");
    ld.reloc = fopen(ld.filname, "r");
    if (!ld.reloc)
        error(2, "cannot open");

    // The first word tells an archive (magic ARMAG) from a plain object file.
    if (!fgetint(ld.text, &c))
        error(1, "unexpected EOF");
    if (c != ARMAG)
        return 0; // regular file
    if (!fgetarhdr(ld.text, &ld.archdr))
        return 1; // regular archive
    // A randomized archive begins with a special "__.SYMDEF" member.
    if (strncmp(ld.archdr.ar_name, SYMDEF, sizeof(ld.archdr.ar_name)))
        return 1; // regular archive
    // It is only trustworthy if not older than the archive file itself.
    fstat(fileno(ld.text), &x);
    if (x.st_mtime > ld.archdr.ar_date + 2)
        return 3; // out of date archive
    return 2;     // randomized archive
}

//
// Guard against overflowing the liblist[] array of remembered member offsets.
//
void check_liblist(void)
{
    if (ld.libp >= &ld.liblist[LLSIZE])
        error(2, "library table overflow");
}

//
// Pass-1 visit of one archive member at byte offset `nloc`.  Reads the member's
// archive header, then scans it like an object file (scan_object) but in "library
// mode": the member is pulled into the link only if it defines a symbol that is
// currently needed.  When it is, its offset is remembered in liblist[] so pass 2
// can find it again; returns 1 if a member was processed, 0 at end of archive.
//
int scan_member(long nloc)
{
    char *cp;

    fseek(ld.text, nloc, 0);
    if (!fgetarhdr(ld.text, &ld.archdr)) {
        *ld.libp++ = -1; // mark end of this archive's member list
        check_liblist();
        return 0;
    }
    unsigned len = sizeof(ld.archdr.ar_name) + 1;
    cp = malloc(len);
    if (!cp) {
        error(2, "out of memory");
    } else {
        strncpy(cp, ld.archdr.ar_name, len);
        cp[sizeof(ld.archdr.ar_name)] = '\0';
        if (scan_object(nloc + ARHDRSZ, 1, make_file_symbol(cp, 0)))
            *ld.libp++ = nloc;
        free(cp);
        check_liblist();
    }
    return 1;
}

//
// Read a randomized archive's table of contents (the "__.SYMDEF" member) into
// ld.rantab.  Each entry pairs an exported symbol name with the byte offset of
// the member that defines it, so we can pull in members on demand without
// scanning the whole archive.  Records the count in ld.tnum.
//
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

//
// One sweep over the table of contents: for every entry whose symbol is
// currently undefined, pull in the member that defines it (scan_member).  Pulling
// in a member can satisfy some references but introduce new undefined ones, so
// the caller calls this repeatedly until a sweep loads nothing more.  Returns
// nonzero if this sweep loaded at least one member (i.e. ld.libp advanced).
//
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

//
// Free the symbol-name strings allocated while reading the table of contents.
//
void free_ranlib(void)
{
    struct ranlib *p;

    for (p = ld.rantab; p < ld.rantab + ld.tnum; ++p)
        free(p->ran_name);
}
