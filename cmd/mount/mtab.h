/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/mtab -- the table mount(1M) and umount(1M) keep between them.  Task C4f.
// mtab.c is the implementation and include/man/mtab.5 the specification.
//
#ifndef MTAB_H
#define MTAB_H

#include <sys/param.h> // NMOUNT

#define MTAB "/etc/mtab"

// A name field.  v7's 32 was a PDP-11 convenience and nothing here; what bounds a path on
// this machine is the stack frame this table is NOT in (it is bss) and the line buffer
// below, so the number is free and 64 holds every path the image has.
#define NAMSIZ 64

// ONE SLOT PER KERNEL MOUNT SLOT, not v7's private 16.  mount[0] is the root's, claimed by
// iinit() and never named here, so NMOUNT - 1 entries is all this file can ever hold and the
// spare costs one struct.  cmd/icheck/README.md's rule: a fixed table is a ceiling somebody
// chose against different hardware -- ask what bounds it here before keeping the number.
// NMOUNT is 8 now rather than 2, so the spare is a seventh of the table instead of half of
// it, and the table is 184 words of bss against a 28,672-word address space.
#define NMTAB NMOUNT

struct mtab {
    char m_spec[NAMSIZ]; // the block special file, as mount(1M) was given it
    char m_dir[NAMSIZ];  // where it was mounted
    int m_ro;            // mounted read-only
};

extern struct mtab mtab[NMTAB];
extern int nmtab;

void mtabread(void);
int mtabwrite(void);
int mtabname(char *dst, const char *src, const char *what);
int mtabfind(const char *spec);

#endif // MTAB_H
