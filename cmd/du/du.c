/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// du -- how many blocks a directory tree costs.
//
// The v7 program, unchanged in what it does: stat(2) each name, sum the block counts,
// recurse into directories by chdir(2) and a raw read(2) of the directory itself, and
// count a multiply-linked file only once.  It touches no device and needs no privilege,
// which is what separates it from df(1) and quot(1) beside it -- see ../df/README.md for
// what those two had to learn about the raw disk, none of which applies here.
//
// A BLOCK IS 1024 BYTES IN WHAT THIS PRINTS, and 3072 in what it counts.  The filesystem's
// block is BSIZE == 3072; this reports KBPB == 3 of them per block (sys/param.h), so that a
// number means something without knowing BSIZE.  Two consequences worth expecting: every
// count is a MULTIPLE OF 3, the smallest thing that can be allocated being one 3072-byte
// block; and a number is half a PDP-11's rather than a sixth, v7's block having been 512
// bytes.  THE MULTIPLY IS AT THE printf and nowhere else -- every count in this file is a
// filesystem block until it is printed.  du.1 says so; ../README.md SS4 is the rule.
//
// WHAT THE PORT HAD TO CHANGE, beyond the mechanical C11 pass.  Three of these are about
// one fact -- a struct direct is FOUR WORDS, 24 bytes, where v7's was 16 -- and the first
// is the most dangerous line in the file:
//
//  1. `entries = dsize>>4' meant "sixteen bytes to an entry".  Left alone it walks 1.5x
//     as many entries as were read, off the end of dentry[] and into the frame.  It is a
//     divide by sizeof(struct direct) now, which is also not a shift: 24 is not a power
//     of two any more than BSIZE is.
//
//  2. The 512-byte read chunk was "one PDP-11 block of entries".  It is sizeof(dentry),
//     which is what it always meant; the buffer is the unit, not the filesystem.
//
//  3. `blocks = (st_size + BSIZE-1) >> BSHIFT' does not compile, and that is the point:
//     there IS no BSHIFT in this port and there cannot be (sys/param.h).  It is a divide.
//
//  4. dentry[] IS ON A RECURSIVE FRAME, and the stack is 4,096 words with nothing checking
//     it (../README.md SS6).  At v7's 32 entries that is 128 words per directory level; it
//     is 16 entries here, 64 words, so a deep tree has room.  It cannot be static: it is
//     live across the recursive call.
//
//  5. d_name IS NOT NUL-TERMINATED ON DISK when a name fills DIRSIZ (../README.md SS5), so
//     v7's `strcmp(dp->d_name, ".")' reads past the field.  ls(1) had to make the same fix.
//     Every name is copied into a bounded local first now, and that copy is what the path
//     is built from.
//
//  6. THE PATH APPEND HAD NO BOUND AT ALL, and it is the worst of the unbounded buffers any
//     port here has met, because it accumulates: one component per recursion level into a
//     fixed path[PATHSIZ].  It is bounded, and so is the backward scan in the chdir("..")
//     recovery path, which walked off the front of the buffer if there was no `/' left.
//     ../README.md SS6: every port so far has had to bound one.
//
//  7. `char *rindex();' -- rindex(3) is declared by no header in this tree, though libc
//     still has one.  strrchr(3) is the spelling <string.h> offers.
//
//  8. Five `long's and a `%ld'.  A long is one word here; all of them are int.
//
// TWO UPSTREAM BUGS, fixed rather than carried, and neither is about this machine:
//
//   * the hard-link table was scanned `i <= linked', one slot past what was populated.  It
//     read a zeroed bss entry, so it was harmless only because no real i-number is 0.
//
//   * descend() returned `0' from the link-already-counted path and `0L' from the other
//     two, in a function declared long.  Moot now that everything is int, but it is why
//     the return type is stated once and not three times.
//
// WHAT IS DELIBERATELY LEFT.  du.1's two BUGS are still true and still documented: a
// non-directory argument prints nothing without -a, and more than ML distinct linked files
// makes the excess count more than once.  Both are v7's design, not defects of this port.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each, and sys/dir.h, which is the precedent.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// What this file assumes of the layout, asserted rather than re-derived (../TODO.md, task
// C4).  sys/dir.h carries the rest -- that an entry is DIRWORDS words and that DIRPB of
// them tile a block.
_Static_assert(sizeof(struct direct) == DIRENTSZ, "a directory entry must be DIRENTSZ bytes");
_Static_assert(BSIZE % sizeof(struct direct) == 0, "entries must tile a block");
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");

#define EQ(x, y) (strcmp(x, y) == 0)
#define ML       1000 // distinct multiply-linked files remembered; du.1's second BUG
#define PATHSIZ  256  // the path being built, and the argument it starts from

// Sixteen entries, not v7's thirty-two: this array is on a RECURSIVE frame and an entry is
// now four words.  See the header.
#define NDENT 16

// v7 rationed descriptors against NOFILE with a bare `10'.  Same number, said properly.
#define DIRFDMAX (NOFILE / 2)

static struct stat Statb;
static char path[PATHSIZ], name[PATHSIZ];
static int Aflag = 0, Sflag = 0, Noarg = 0;
static struct {
    int dev, ino;
} ml[ML];

static int descend(char *np, char *fname);

int main(int argc, char **argv)
{
    int i = 1;
    int blocks;
    char *np;

    if (argc > 1) {
        if (EQ(argv[i], "-s")) {
            ++i;
            ++Sflag;
        } else if (EQ(argv[i], "-a")) {
            ++i;
            ++Aflag;
        }
    }
    if (i == argc)
        ++Noarg;

    do {
        // Bounded, where v7 copied from argv with strcpy(3) and hoped.  A name that does
        // not fit could not be walked anyway -- the append below needs room for a
        // component per level -- so it is refused here rather than truncated into
        // something that names a different file.
        const char *arg = Noarg ? "." : argv[i];
        if (strlen(arg) >= PATHSIZ) {
            fprintf(stderr, "du: %s: name too long\n", arg);
            continue;
        }
        strcpy(path, arg);
        strcpy(name, path);

        if ((np = strrchr(name, '/')) != NULL) {
            *np++ = '\0';
            if (chdir(*name ? name : "/") == -1) {
                fprintf(stderr, "cannot chdir()\n");
                exit(1);
            }
        } else
            np = path;

        blocks = descend(path, *np ? np : ".");
        if (Sflag)
            printf("%d\t%s\n", blocks * KBPB, path);
    } while (++i < argc);

    exit(0);
}

//
// Sum `fname', which is reached from the current directory, and print as the flags ask.
// `np' is the path buffer being grown in place -- always path[] -- and the name being
// summed is its tail.
//
static int descend(char *np, char *fname)
{
    int dir = 0; // open directory, 0 meaning `not open'
    int offset, dsize, entries, dirsize;
    struct direct dentry[NDENT];
    struct direct *dp;
    char nbuf[DIRSIZ + 1];
    int i, n, endoff;
    int blocks = 0;

    if (stat(fname, &Statb) < 0) {
        fprintf(stderr, "--bad status < %s >\n", name);
        return 0;
    }
    if (Statb.st_nlink > 1 && (Statb.st_mode & S_IFMT) != S_IFDIR) {
        static int linked = 0;

        // `i < linked', not v7's `i <= linked': the entry at [linked] has not been
        // written yet.
        for (i = 0; i < linked; ++i) {
            if (ml[i].ino == Statb.st_ino && ml[i].dev == Statb.st_dev)
                return 0;
        }
        if (linked < ML) {
            ml[linked].dev = Statb.st_dev;
            ml[linked].ino = Statb.st_ino;
            ++linked;
        }
    }
    blocks = (Statb.st_size + BSIZE - 1) / BSIZE;

    if ((Statb.st_mode & S_IFMT) != S_IFDIR) {
        if (Aflag)
            printf("%d\t%s\n", blocks * KBPB, np);
        return blocks;
    }

    // Where a component gets appended: an index, so the bound test is on the index.
    endoff = (int)strlen(np);
    if (endoff > 0 && np[endoff - 1] == '/')
        --endoff;

    dirsize = Statb.st_size;
    if (chdir(fname) == -1)
        return 0;

    for (offset = 0; offset < dirsize; offset += (int)sizeof dentry) { // each bufferful
        dsize = (int)sizeof dentry;
        if (dsize > dirsize - offset)
            dsize = dirsize - offset;
        if (!dir) {
            if ((dir = open(".", 0)) < 0) {
                fprintf(stderr, "--cannot open < %s >\n", np);
                goto ret;
            }
            if (offset)
                lseek(dir, (off_t)offset, 0);
            if (read(dir, (char *)dentry, dsize) < 0) {
                fprintf(stderr, "--cannot read < %s >\n", np);
                goto ret;
            }
            // Ration descriptors: a deep tree would otherwise hold one open per level.
            if (dir > DIRFDMAX) {
                close(dir);
                dir = 0;
            }
        } else if (read(dir, (char *)dentry, dsize) < 0) {
            fprintf(stderr, "--cannot read < %s >\n", np);
            goto ret;
        }

        // A divide, not v7's `dsize>>4': an entry is DIRENTSZ == 24 bytes here.
        for (dp = dentry, entries = dsize / (int)sizeof(struct direct); entries; --entries, ++dp) {
            if (dp->d_ino == 0)
                continue;

            // d_name fills DIRSIZ with no NUL when the name is that long, so it is copied
            // out before anything compares or appends it.
            for (i = 0; i < DIRSIZ && dp->d_name[i]; ++i)
                nbuf[i] = dp->d_name[i];
            nbuf[i] = '\0';
            if (nbuf[0] == '\0' || EQ(nbuf, ".") || EQ(nbuf, ".."))
                continue;

            // `/', the name, and a NUL.  v7 wrote this with no test of any kind, into a
            // buffer that had already been appended to once per level above.
            if (endoff + 1 + i + 1 > PATHSIZ) {
                fprintf(stderr, "--path too long < %s/%s >\n", np, nbuf);
                continue;
            }
            n       = endoff;
            np[n++] = '/';
            for (i = 0; nbuf[i]; ++i)
                np[n++] = nbuf[i];
            np[n] = '\0';

            blocks += descend(np, &np[endoff + 1]);
        }
    }
    np[endoff] = '\0';
    if (!Sflag)
        printf("%d\t%s\n", blocks * KBPB, np);
ret:
    if (dir)
        close(dir);
    if (chdir("..") == -1) {
        np[endoff] = '\0';
        fprintf(stderr, "Bad directory <%s>\n", np);
        // Bounded, where v7's `while(*--endofname != '/');' ran off the front of the
        // buffer when there was no slash left to find.
        while (endoff > 0 && np[--endoff] != '/')
            ;
        np[endoff] = '\0';
        if (chdir(np) == -1)
            exit(1);
    }
    return blocks;
}
