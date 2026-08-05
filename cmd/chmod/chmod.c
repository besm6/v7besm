/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// chmod -- change mode.
//
// The v7 program, unchanged in what it does: parse one mode argument -- absolute octal, or
// symbolic [ugoa][+-=][rwxstugo] in comma-separated clauses -- and hand the result to
// chmod(2) for each file named after it.  The first of task C1c's four (../README.md), and the
// only one of them that is a parser rather than a wrapper.
//
// The C11 pass is the usual one (../init/README.md is the worked example): a prototype and an
// explicit return type on main() and on all five helpers, `static' on them and on the three
// file-scope objects, the implicit-int `register i;' / `register o, m, b;' declarations given
// their type, `while (o = what())' parenthesised, the missing <stdlib.h> added, and 4-space
// indentation.  main() returns rather than exit()s.
//
// TWO CHANGES BEYOND THE MECHANICAL PASS, both forced by this tree rather than chosen:
//
// 1.  abs() BECAME absmode().  v7's parser calls its octal scanner abs(), which in C11 is a
//     reserved external name -- and not merely reserved: this libc really defines it
//     (lib/libc/gen/abs.c, int abs(int)).  v7's takes no argument and reads the global `ms'.
//     Nothing would have failed to link, since the program's own definition satisfies its own
//     call and abs.o is then never pulled from the archive, so the collision would have sat
//     there silently until something else in the file wanted the real abs().  Renamed, on the
//     precedent of ../mkdir/mkdir.c's readdir->listdir and mkdir->makedir.
//
// 2.  newmode()'s parameter and where()'s are `int' rather than `unsigned'.  v7 wrote
//     `unsigned nm', and `nm &= ~m' then computes ~m as a NEGATIVE int and converts it --
//     which on this machine is a bare reinterpretation of the word and not C11 6.3.1.3p2's
//     modulo adjustment, because an int occupies bits 41-1 and an unsigned all 48
//     (../../doc/Besm6_Data_Representation.md states the deviation).  It happens to come out
//     right, every value here being a mask of at most 07777, but it is precisely the shape
//     that note exists to warn about.  A mode is twelve bits and chmod(2) is declared
//     `int chmod(const char *, int)', so plain int makes the question not arise.
//
// NO BOUNDS CHECK WAS ADDED, AND THAT IS THE FINDING.  Every sibling in tasks C1a-C1c gained
// one -- ../mkdir, ../rmdir, ../ln, ../cp, ../mv all build a path in a fixed automatic -- and
// a reader will look for one here.  There is nothing to bound: chmod has no buffer at all, no
// strcpy, no sprintf and no strcat.  It walks argv[1] in place.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No long, no %D, no struct direct -- chmod
// never reads a directory, so DIRSIZ being 18 here does not reach it.
//
// TWO THINGS THAT LOOK LIKE BUGS HERE AND ARE NOT.  The `newmode(0)' in main() is a
// validation pass: it parses the mode argument once, before any file is touched, so that an
// unparsable mode kills the program instead of half-applying itself -- and it works by
// consuming the global cursor `ms', which is exactly why the loop below resets `ms = argv[1]'
// before every file.  And `where()' reaches a label inside its own switch with `goto dup',
// which is legal C11 and says what it means: the three who-letters share one tail.
//
// THREE THINGS THIS MACHINE MAKES TRUE THAT chmod.1.umm NEVER SAID, all now Note:d there:
//
//  - `chmod +s file' DOES NOTHING, here and in v7 alike.  With the who omitted, who() answers
//    ALL & ~um = 01777, SETID is 06000, and the AND of the two is zero.  The page's "the
//    letter s is only useful with u or g" understates it: outside u and g it is a no-op.
//  - `chmod +t' DOES work (STICKY 01000 is inside ALL) -- but only for the super-user, since
//    chmod() in kernel/sys4.c clears ISVTX whenever u_uid is non-zero.  An ordinary owner's
//    `chmod +t' succeeds and changes nothing.
//  - THE OMITTED-WHO DEFAULT DEPENDS ON THE UMASK, and CMASK is 0 in <sys/param.h> while no
//    shell here has a umask builtin.  So it is plainly `a' today, and every expectation that
//    names a mode rests on that -- the same footing ../mkdir/mkdir.c's 0777 rests on.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#define USER  05700 /* user's bits */
#define GROUP 02070 /* group's bits */
#define OTHER 00007 /* other's bits */
#define ALL   01777 /* all (note absence of setuid, etc) */

#define READ   00444 /* read permit */
#define WRITE  00222 /* write permit */
#define EXEC   00111 /* exec permit */
#define SETID  06000 /* set[ug]id */
#define STICKY 01000 /* sticky bit */

static char *ms;
static int um;
static struct stat st;

// The absolute form: an octal number, or nothing at all.
static int absmode(void)
{
    int c, i;

    i = 0;
    while ((c = *ms++) >= '0' && c <= '7')
        i = (i << 3) + (c - '0');
    ms--;
    return i;
}

// The `who' of one clause.  Omitted, it is every bit the umask does not hold back.
static int who(void)
{
    int m;

    m = 0;
    for (;;)
        switch (*ms++) {
        case 'u':
            m |= USER;
            continue;
        case 'g':
            m |= GROUP;
            continue;
        case 'o':
            m |= OTHER;
            continue;
        case 'a':
            m |= ALL;
            continue;
        default:
            ms--;
            if (m == 0)
                m = ALL & ~um;
            return m;
        }
}

// The `op' of one clause, or 0 when the clause is over.
static int what(void)
{
    switch (*ms) {
    case '+':
    case '-':
    case '=':
        return *ms++;
    }
    return 0;
}

// The `permission': either letters, or another who's bits copied out of the current mode.
static int where(int om)
{
    int m;

    m = 0;
    switch (*ms) {
    case 'u':
        m = (om & USER) >> 6;
        goto dup;
    case 'g':
        m = (om & GROUP) >> 3;
        goto dup;
    case 'o':
        m = (om & OTHER);
    dup:
        m &= (READ | WRITE | EXEC);
        m |= (m << 3) | (m << 6);
        ++ms;
        return m;
    }
    for (;;)
        switch (*ms++) {
        case 'r':
            m |= READ;
            continue;
        case 'w':
            m |= WRITE;
            continue;
        case 'x':
            m |= EXEC;
            continue;
        case 's':
            m |= SETID;
            continue;
        case 't':
            m |= STICKY;
            continue;
        default:
            ms--;
            return m;
        }
}

// The mode argument applied to one file's current mode.  Consumes `ms'.
static int newmode(int nm)
{
    int o, m, b;

    m = absmode();
    if (!*ms)
        return m;
    do {
        m = who();
        while ((o = what()) != 0) {
            b = where(nm);
            switch (o) {
            case '+':
                nm |= b & m;
                break;
            case '-':
                nm &= ~(b & m);
                break;
            case '=':
                nm &= ~m;
                nm |= b & m;
                break;
            }
        }
    } while (*ms++ == ',');
    if (*--ms) {
        fprintf(stderr, "chmod: invalid mode\n");
        exit(255);
    }
    return nm;
}

int main(int argc, char **argv)
{
    int i;
    char *p;
    int status = 0;

    if (argc < 3) {
        fprintf(stderr, "Usage: chmod [ugoa][+-=][rwxstugo] file ...\n");
        return 255;
    }
    ms = argv[1];
    um = umask(0);

    // Parse it once before touching anything: an unparsable mode must not half-apply.
    newmode(0);

    for (i = 2; i < argc; i++) {
        p = argv[i];
        if (stat(p, &st) < 0) {
            fprintf(stderr, "chmod: can't access %s\n", p);
            ++status;
            continue;
        }
        ms = argv[1];
        if (chmod(p, newmode(st.st_mode)) < 0) {
            fprintf(stderr, "chmod: can't change %s\n", p);
            ++status;
            continue;
        }
    }
    return status;
}
