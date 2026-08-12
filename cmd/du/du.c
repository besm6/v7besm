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
// filesystem block until it is printed.  du.1.umm says so; ../README.md SS4 is the rule.
//
// THE DIRECTORY IS READ WITH opendir(3), task C24, and with it went the three defects v7's
// layout arithmetic carried into this file -- `dsize>>4' for a sixteen-byte entry, a 512-byte
// read chunk that meant a PDP-11 block, and an unterminated d_name handed to strcmp.  What
// stays is the BATCH: descend() still fills an array of names and only then recurses over it,
// because that is what lets the descriptor be dropped and re-taken between batches.
// README.md is the account of both.
//
// WHAT THE PORT HAD TO CHANGE, beyond the mechanical C11 pass:
//
//  1. `blocks = (st_size + BSIZE-1) >> BSHIFT' does not compile, and that is the point:
//     there IS no BSHIFT in this port and there cannot be (sys/param.h).  It is a divide.
//
//  2. THE BATCH IS ON A RECURSIVE FRAME, and the stack is 4,096 words with nothing checking
//     it (../README.md SS6).  Sixteen names are 51 words per directory level, where v7's
//     thirty-two struct direct would have been 128.  It cannot be static: it is live across
//     the recursive call.
//
//  3. THE PATH APPEND HAD NO BOUND AT ALL, and it is the worst of the unbounded buffers any
//     port here has met, because it accumulates: one component per recursion level into a
//     fixed path[PATHSIZ].  It is bounded, and so is the backward scan in the chdir("..")
//     recovery path, which walked off the front of the buffer if there was no `/' left.
//     ../README.md SS6: every port so far has had to bound one.
//
//  4. `char *rindex();' -- rindex(3) is declared by no header in this tree, though libc
//     still has one.  strrchr(3) is the spelling <string.h> offers.
//
//  5. Five `long's and a `%ld'.  A long is one word here; all of them are int.
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
// WHAT IS DELIBERATELY LEFT.  du.1.umm's two BUGS are still true and still documented: a
// non-directory argument prints nothing without -a, and more than ML distinct linked files
// makes the excess count more than once.  Both are v7's design, not defects of this port.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");

#define EQ(x, y) (strcmp(x, y) == 0)
#define ML       1000 // distinct multiply-linked files remembered; du.1.umm's second BUG
#define PATHSIZ  256  // the path being built, and the argument it starts from

// Names per batch.  Sixteen, not v7's thirty-two entries: the array is on a RECURSIVE frame.
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
    DIR *dirp = NULL; // NULL meaning `dropped', not `end of directory'
    struct dirent *dp;
    char names[NDENT][DIRSIZ + 1]; // one batch, read before any of it recurses
    long loc;
    int nb, done = 0;
    int i, k, n, endoff;
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

    if (chdir(fname) == -1)
        return 0;
    if ((dirp = opendir(".")) == NULL) {
        fprintf(stderr, "--cannot open < %s >\n", np);
        goto ret;
    }

    while (!done) {
        // One batch of names, taken before any of them recurses: that is what makes the
        // descriptor droppable below, and it is what the raw reader's bufferful was for.
        for (nb = 0; nb < NDENT;) {
            if ((dp = readdir(dirp)) == NULL) {
                done = 1;
                break;
            }
            if (dp->d_namlen == 0 || EQ(dp->d_name, ".") || EQ(dp->d_name, ".."))
                continue;
            strcpy(names[nb++], dp->d_name);
        }

        // Ration descriptors: a deep tree would otherwise hold one open per level.  The
        // cookie survives the closedir(), and descend() leaves `.' where it found it.
        loc = telldir(dirp);
        if (dirfd(dirp) > DIRFDMAX) {
            closedir(dirp);
            dirp = NULL;
        }

        for (k = 0; k < nb; ++k) {
            i = (int)strlen(names[k]);

            // `/', the name, and a NUL.  v7 wrote this with no test of any kind, into a
            // buffer that had already been appended to once per level above.
            if (endoff + 1 + i + 1 > PATHSIZ) {
                fprintf(stderr, "--path too long < %s/%s >\n", np, names[k]);
                continue;
            }
            n       = endoff;
            np[n++] = '/';
            for (i = 0; names[k][i]; ++i)
                np[n++] = names[k][i];
            np[n] = '\0';

            blocks += descend(np, &np[endoff + 1]);
        }

        if (!done && dirp == NULL) {
            if ((dirp = opendir(".")) == NULL) {
                fprintf(stderr, "--cannot open < %s >\n", np);
                goto ret;
            }
            seekdir(dirp, loc);
        }
    }
    np[endoff] = '\0';
    if (!Sflag)
        printf("%d\t%s\n", blocks * KBPB, np);
ret:
    if (dirp)
        closedir(dirp);
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
