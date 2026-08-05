/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// cp -- copy files.
//
// The v7 program, unchanged in what it does: `cp f1 f2' or `cp f1 ... fn d'.  Open the
// source, fstat it for the mode, build the target name if the target is a directory, refuse a
// copy onto the same inode, creat() and then read/write until the source runs out.
//
// cp lands before mv in task C1b (../README.md) and not by taste: mv's cross-device path is a
// literal execl("/bin/cp", ...) (../mv/mv.c), so mv is only half a program until this one is
// on the image.  That path is in fact unreachable here -- one EC-5052 is the whole store, so
// link() never comes back EXDEV -- but the dependency is in the source and is honoured.
//
// The C11 pass is the usual one (../init/README.md is the worked example): a prototype and an
// explicit return type on both functions, `static' on the two struct stats, the two buffers
// and copy(), open(from, 0) spelled O_RDONLY, and the two assignments-in-conditions
// parenthesised.  main() returns rather than exit()s.
//
// THREE CHANGES BEYOND THE MECHANICAL PASS:
//
//  1. `#define BSIZE 512' is gone, and the I/O buffer is BUFSIZ.  The name was wrong twice
//     over.  A filesystem block here is 3,072 bytes (include/sys/param.h), so a 512-byte read
//     is six system calls per block and five of them unaligned; BUFSIZ is 3,072 for exactly
//     that reason (include/stdio.h).  And the name itself is a trap: cp.c includes no header
//     that reaches <sys/param.h> TODAY, so the local BSIZE never meets the real one -- but the
//     first include that does reach it turns this into a redefinition, or worse a silent
//     disagreement about what a block is.
//
//  2. THE TARGET PATH WAS BUILT INSIDE THE I/O BUFFER.  v7 assembled "d/basename" into iobuf
//     and then set `to = iobuf', leaving the target name pointing into the buffer the copy
//     loop is about to read into.  It survives only because the last use of `to' (the creat
//     and its error message) happens before the first read() -- an invariant nothing states
//     and any later edit breaks, and the report would then print whatever the first block of
//     the source file happened to contain.  The path has its own buffer now, and a bound: v7
//     had none at all, and both operands come from argv.  The overrun that bound prevents is
//     in BSS rather than on the stack, which is quieter, not safer: it lands in whatever the
//     linker put next.
//
//  3. The "cannot copy file to itself" arm returned without closing the source descriptor.
//     `cp f1 ... fn d' calls copy() once per argument in one process, so the leak is per
//     refused file and NOFILE is 20 (include/sys/param.h).  One close().
//
// TWO THINGS THAT LOOK LIKE BUGS HERE AND ARE NOT.  Noted so the next reader does not
// "fix" them:
//
//  * `while ((n = read(...)))' treats a -1 as true, and the n < 0 test is INSIDE the loop.
//    That is deliberate and correct: a read error must be reported, not mistaken for
//    end of file.
//
//  * creat(to, mode) is handed the full st_mode, S_IFREG bit and all.  The kernel masks it --
//    maknode(uap->fmode & 07777 & ~ISVTX), kernel/sys2.c -- so the type bits cannot reach the
//    inode.  What the mask also does is drop the sticky bit and apply the umask, which is why
//    cp.1.umm now says cp does not reproduce a mode exactly.
//
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Room for "directory/basename".  v7 had no bound of its own; 512 is what its reuse of the
// 512-byte iobuf amounted to, kept as a number that now means something.  86 words of bss.
#define PATHBUF 512

static struct stat stbuf1, stbuf2;
static char iobuf[BUFSIZ];
static char pathbuf[PATHBUF];

static int copy(char *from, char *to);

int main(int argc, char **argv)
{
    int i, r;

    if (argc < 3)
        goto usage;
    if (argc > 3) {
        if (stat(argv[argc - 1], &stbuf2) < 0)
            goto usage;
        if ((stbuf2.st_mode & S_IFMT) != S_IFDIR)
            goto usage;
    }
    r = 0;
    for (i = 1; i < argc - 1; i++)
        r |= copy(argv[i], argv[argc - 1]);
    return r;
usage:
    fprintf(stderr, "Usage: cp: f1 f2; or cp f1 ... fn d2\n");
    return 1;
}

static int copy(char *from, char *to)
{
    int fold, fnew, n;
    char *p1, *p2, *bp;
    int mode;

    if ((fold = open(from, O_RDONLY)) < 0) {
        fprintf(stderr, "cp: cannot open %s\n", from);
        return 1;
    }
    fstat(fold, &stbuf1);
    mode = stbuf1.st_mode;

    // Is the target a directory?  Then the name to create is "to/" plus the last component
    // of `from'.  The bound v7 had not: the result cannot exceed to + '/' + from + NUL, and
    // both of those come straight from argv.
    if (stat(to, &stbuf2) >= 0 && (stbuf2.st_mode & S_IFMT) == S_IFDIR) {
        if ((int)strlen(to) + (int)strlen(from) + 2 > (int)sizeof pathbuf) {
            fprintf(stderr, "cp: %s/%s: name too long\n", to, from);
            close(fold);
            return 1;
        }
        p1 = from;
        p2 = to;
        bp = pathbuf;
        while ((*bp++ = *p2++))
            ;
        bp[-1] = '/';
        p2     = bp;
        while ((*bp = *p1++))
            if (*bp++ == '/')
                bp = p2; // a new component starts; forget the one just copied
        to = pathbuf;
    }
    if (stat(to, &stbuf2) >= 0) {
        if (stbuf1.st_dev == stbuf2.st_dev && stbuf1.st_ino == stbuf2.st_ino) {
            fprintf(stderr, "cp: cannot copy file to itself.\n");
            close(fold);
            return 1;
        }
    }
    if ((fnew = creat(to, mode)) < 0) {
        fprintf(stderr, "cp: cannot create %s\n", to);
        close(fold);
        return 1;
    }
    while ((n = read(fold, iobuf, sizeof iobuf))) {
        if (n < 0) {
            fprintf(stderr, "cp: read error\n");
            close(fold);
            close(fnew);
            return 1;
        } else if (write(fnew, iobuf, n) != n) {
            fprintf(stderr, "cp: write error.\n");
            close(fold);
            close(fnew);
            return 1;
        }
    }
    close(fold);
    close(fnew);
    return 0;
}
