/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// touch -- update the date last modified of a file.
//
// The v7 program in what it does from the outside: for each name, bring its date up to now,
// and create the file if it does not exist unless -c said not to.  The last of task C1c's four
// (../README.md).
//
// THE MIDDLE OF IT IS DELIBERATELY NOT v7's, and this is the one divergence in task C1c.  v7's
// touch has no utime(2) in it at all: it opens the file O_RDWR, reads its first byte, seeks
// back and writes the same byte again, letting the write move the modified time.  That works,
// and it is what v7's touch.1 described -- but it needs read AND write permission on a file the
// caller may only own, it cannot touch a read-only file at all, it re-creat()s (and so
// truncates) any file of zero length because there is no byte in one to rewrite, and it puts a
// write down a path where nothing was meant to change.  This port calls utime(2) instead:
//
//      time(&now);
//      t[0] = t[1] = now;
//      utime(name, t);
//
// utime() here takes a TWO-ELEMENT time_t VECTOR -- accessed, then updated -- and not POSIX's
// `struct utimbuf'; see <unistd.h> and lib/libc/man/utime.2.  time_t is one word.  The kernel
// (utime() in kernel/sys4.c) has no "a null pointer means now" branch, so the vector is
// mandatory, and it gates the call on owner() -- the file's owner or the super-user -- which
// is a weaker requirement than v7's open-for-writing, not a stronger one.  touch.1 is rewritten
// to say all of this rather than corrected in place; it is the DESCRIPTION that stopped being
// true, not a detail of it.
//
// The consequences worth knowing: touch now sets BOTH times, not just the modified one; it no
// longer truncates an empty file; and it no longer writes to the file at all, so a full
// filesystem cannot make it fail halfway.
//
// The C11 pass is the usual one (../init/README.md is the worked example): a prototype and an
// explicit return type on main() and on touchfile(), which v7 defined BELOW its only call;
// `static' on the helper; the missing <string.h> added, strcmp having been used with no
// declaration whatever; <sys/types.h> and <sys/stat.h> moved up from the middle of the file to
// the top with the rest; and 4-space indentation.
//
// TWO CHANGES BEYOND THE MECHANICAL PASS, besides utime():
//
// 1.  v7's main() FALLS OFF THE END -- it returns no value and calls no exit(), so the process
//     status was whatever the accumulator happened to hold.  It counts its failures now and
//     returns them, and touchfile() returns 0 or 1 instead of returning nothing from a
//     function implicitly declared to return int.
//
// 2.  The `else' in the stat() failure path is braced.  It binds to `if (force)', which is
//     what was meant, but the source did not read that way.
//
// THE -c FLAG IS POSITIONAL AND STAYS THAT WAY: v7 scans one argument list once, so a -c
// written after a file name affects only the names after it.  Kept, and touch.1 now says so.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No %D, no struct direct -- touch never reads a
// directory.  The one `long' in task C1c was v7's
// `lseek(fd, 0L, 0)', and the rewrite above took it with the rest of that path.
//
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int touchfile(int force, const char *name)
{
    struct stat stbuff;
    time_t now, t[2];
    int fd;

    if (stat(name, &stbuff) < 0) {
        if (!force) {
            fprintf(stderr, "touch: file %s does not exist.\n", name);
            return 1;
        }
        if ((fd = creat(name, 0666)) < 0)
            goto bad;
        close(fd);
        return 0;
    }

    // Both times, to now.  v7 moved the modified time by rewriting a byte; see the head of
    // this file for why that is gone.
    time(&now);
    t[0] = now;
    t[1] = now;
    if (utime(name, t) < 0)
        goto bad;
    return 0;

bad:
    fprintf(stderr, "Cannot touch %s\n", name);
    return 1;
}

int main(int argc, char *argv[])
{
    int i;
    int force  = 1;
    int status = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0)
            force = 0;
        else
            status += touchfile(force, argv[i]);
    }
    return status;
}
