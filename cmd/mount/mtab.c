/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/mtab -- the table mount(1M) and umount(1M) keep between them.  Task C4f.
//
// ONE HOME FOR THE FORMAT.  v7 gives each of the two programs its own `struct mtab', its own
// read, its own write and its own idea of how a name is trimmed -- four copies of one layout
// across two files, and umount.c's copy is the one with the bug.  This file is that layout
// written once; ../umount/CMakeLists.txt compiles it a second time rather than duplicating
// it.  include/man/mtab.5 is the specification and is kept in step with this file.
//
// THE FORMAT IS TEXT HERE, and the machine forced that rather than taste.  v7's table is a
// binary record blitted straight out of the struct:
//
//	struct mtab { char file[32]; char spec[32]; } mtab[16];
//	write(mf, (char *)mtab, (mp - mtab + 1) * 2 * NAMSIZ);
//
// A `char[32]' occupies ceil(32/6) = 6 words = 36 char-units on this machine and the next
// member starts on a word boundary (doc/Besm6_Data_Representation.md), so sizeof(struct
// mtab) is 72 and not 64.  v7's own arithmetic therefore writes 64-byte records out of
// 72-byte objects: every entry after the first is read back eight bytes out of step, and
// `mount' with no argument prints rubbish.  The three ways out were the record widened to a
// whole number of words, the struct dropped for a flat char array with index arithmetic, and
// this one.  Text wins twice over: nothing computes an offset at all, and the file can be
// diffed by the test that writes it.
//
// It cost the two programs their basename stripping as well, which is where three of their
// five `char *' comparisons lived (../README.md SS2).  v7 stored `md1' rather than
// `/dev/md1' because 32 bytes was tight; a line is as long as it needs to be, so the special
// file is recorded as it was given and umount matches the string the user typed.
//
// Every scan below is by INDEX and not by pointer, which is SS2's rule rather than a habit:
// `p < end' between two char * compares the byte offset before the word address and comes
// out scrambled.
//

#include "mtab.h"

#include <stdio.h>
#include <string.h>

struct mtab mtab[NMTAB];
int nmtab;

static int field(const char *line, int *ip, char *dst);

//
// Load the table.  A missing /etc/mtab is an empty table and not an error: nothing stages
// one onto the image, so the first mount(1M) of a boot creates it.  v7 leaned on an
// unchecked open(2) returning -1 and read(2) failing into zeroed bss for the same effect.
//
void mtabread(void)
{
    FILE *f;
    char line[2 * NAMSIZ + 32];
    char flag[NAMSIZ];
    int i;

    nmtab = 0;
    f     = fopen(MTAB, "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (nmtab >= NMTAB) {
            fprintf(stderr, "%s: more than %d entries, the rest ignored\n", MTAB, NMTAB);
            break;
        }
        i = 0;
        if (!field(line, &i, mtab[nmtab].m_spec))
            continue; // a blank line
        if (!field(line, &i, mtab[nmtab].m_dir))
            continue; // and a line with no mount point is not one
        mtab[nmtab].m_ro = 0;
        if (field(line, &i, flag))
            mtab[nmtab].m_ro = strcmp(flag, "ro") == 0;
        nmtab++;
    }
    fclose(f);
}

//
// Write it back.  Returns 0 on success and -1 with a diagnostic otherwise; both callers
// report the failure and still exit successfully, because by the time this runs the mount or
// the unmount has already happened and the kernel is the truth.  mtab(5) has said since v7
// that the file is there so people can look at it.
//
int mtabwrite(void)
{
    FILE *f;
    int i;

    f = fopen(MTAB, "w");
    if (f == NULL) {
        perror(MTAB);
        return -1;
    }
    for (i = 0; i < nmtab; i++)
        fprintf(f, "%s %s%s\n", mtab[i].m_spec, mtab[i].m_dir, mtab[i].m_ro ? " ro" : "");
    if (ferror(f)) {
        perror(MTAB);
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        perror(MTAB);
        return -1;
    }
    return 0;
}

//
// Copy one name into the table in its canonical form -- trailing slashes off, so that
// `umount /dev/md1/' finds what `mount /dev/md1' recorded.  A name too long to hold is
// refused here rather than truncated: a truncated special file is one umount can never
// match, and a truncated mount point is a lie about where the filesystem is.  `what' names
// the argument in the diagnostic.  Returns 0 on failure.
//
int mtabname(char *dst, const char *src, const char *what)
{
    int n;

    for (n = 0; src[n] != 0; n++) {
        if (n >= NAMSIZ - 1) {
            fprintf(stderr, "%s: %s name is longer than %d characters\n", what, src, NAMSIZ - 1);
            return 0;
        }
        dst[n] = src[n];
    }
    if (n == 0) {
        fprintf(stderr, "%s: empty name\n", what);
        return 0;
    }
    while (n > 1 && dst[n - 1] == '/') // n > 1 so that "/" survives being trimmed
        n--;
    dst[n] = 0;
    return 1;
}

//
// The index of the entry naming this special file, or -1.  The argument is matched in the
// same canonical form mtabname() records, so the caller passes what that produced.
//
int mtabfind(const char *spec)
{
    int i;

    for (i = 0; i < nmtab; i++)
        if (strcmp(mtab[i].m_spec, spec) == 0)
            return i;
    return -1;
}

//
// One blank-separated field out of a line, starting at *ip and advancing it.  Copies at most
// NAMSIZ - 1 characters and always terminates; returns 0 when there is no field left.
//
static int field(const char *line, int *ip, char *dst)
{
    int i = *ip;
    int n = 0;
    int c;

    while (line[i] == ' ' || line[i] == '\t')
        i++;
    while ((c = line[i]) != 0 && c != ' ' && c != '\t' && c != '\n') {
        if (n < NAMSIZ - 1)
            dst[n++] = c;
        i++;
    }
    dst[n] = 0;
    *ip    = i;
    return n > 0;
}
