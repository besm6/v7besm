/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// mv -- move or rename files and directories.
//
// The v7 program, unchanged in what it does.  Three forms, which its own usage string names
// and mv.1.umm had never documented in full:
//
//     mv f1 f2            rename a file
//     mv f1 ... fn d      move files into a directory
//     mv d1 d2            rename a directory -- mvdir(), and the whole of the interest
//
// THE THIRD SET-USER-ID PROGRAM ON THIS IMAGE, after ../mkdir and ../rmdir, and for the same
// reason: there is no rename(2) and no mkdir(2), so renaming a directory is done by hand out
// of link() and unlink(), and BOTH REFUSE A DIRECTORY TO ANYONE BUT THE SUPER-USER --
// `if ((ip->i_mode & IFMT) == IFDIR && !suser())' in link() (kernel/sys2.c) and in unlink()
// (kernel/sys4.c).  So /bin/mv is `mode 04755' in ../../scripts/root.manifest, and README.md is the
// account; ../mkdir/README.md is the general one and is not repeated here.
//
// WHICH MAKES THE PLACE OF setuid(getuid()) LOAD-BEARING, and it is v7's own placement, kept
// exactly: it sits AFTER the mvdir() return, so a directory rename keeps the borrowed root
// and every other form of mv drops it before touching anything.  What decides whether a
// directory rename is allowed is then mvdir()'s own access(2) calls on both parents and on
// the directory itself -- and access() asks about the REAL user id, which is the same "borrow
// root, decide on the real uid" contract mkdir(1) and rmdir(1) keep.
//
// The C11 pass is the usual one (../init/README.md is the worked example): prototypes and
// explicit return types on all seven functions, `static' on them and on the two struct stats,
// four untyped `register i;' declarations given a type, the three pre-ANSI `char *f();'
// re-declarations deleted -- `char *sprintf();' was also WRONG, <stdio.h> having declared
// sprintf as returning int since ANSI -- access(p, 2) spelled W_OK, and the missing
// <stdlib.h>, <string.h>, <sys/wait.h> and <unistd.h> added.  <sys/param.h> replaces the
// local `#define ROOTINO 2', which agreed with it; NSIG comes from there too.
//
// FIVE CHANGES BEYOND THE MECHANICAL PASS:
//
//  1. `strcat(dst, target)' INTO AN UNINITIALIZED AUTOMATIC.  The first thing v7 does with
//     `char dst[MAXN+5]' is strcat, not strcpy, so the append starts wherever strlen() finds a
//     zero byte in the stack garbage and runs off the end of the buffer from there.  This one
//     corrupts, and it does it in the worst place in the program: inside the critical section,
//     with every signal ignored, AFTER link(source, target) has already succeeded.  A crash
//     there leaves the directory with two names and a link count that nothing puts back.  It
//     is strcpy() now -- and the whole of dst is built, and bounded, BEFORE the critical
//     section is entered, so that nothing between the link and the last unlink can fail for a
//     reason the program could have found out earlier.
//
//  2. `int status;' WAS UNINITIALIZED and is read after the wait() loop -- which exits either
//     on the child's pid or on -1, and on -1 nothing has been stored.  A cross-device move
//     whose wait() failed would then report whatever was on the stack, and `if (status != 0)'
//     would call a failed copy a success as often as not.  Initialized, the -1 case reported,
//     and the status taken apart with <sys/wait.h>'s WIFEXITED/WEXITSTATUS rather than
//     compared against zero as a whole word.
//
//  3. utime() IS SPELLED WITH A REAL ARRAY.  v7 wrote `utime(target, &s1.st_atime)' and relied
//     on st_atime and st_mtime being adjacent -- which they are here, both one-word time_t
//     (include/sys/stat.h), so it would have worked.  It is an explicit time_t[2] anyway,
//     because a struct's field order is not an interface.  The call also had no declaration
//     anywhere in include/: it is in <unistd.h> now, matching the SYNOPSIS
//     lib/libc/man/utime.2 already carried.
//
//  4. `for (i = 1; i <= NSIG; i++) signal(i, SIG_IGN)' IS OFF BY ONE.  NSIG is 17 and signal
//     numbers run 1..NSIG-1 (include/sys/param.h, include/signal.h), so the last iteration
//     asks the kernel to ignore signal 17, which it refuses.  Harmless -- the return is
//     discarded -- and wrong.  Nothing restores the dispositions afterwards, which is also
//     v7's and is right: mvdir() is the last thing the process does.
//
//  5. THREE MORE UNBOUNDED COPIES, none of them as sharp as the first but all of the same
//     shape -- argv strings into 100-byte automatics, against a 4,096-word stack that nothing
//     checks (../README.md, the porting recipe).  v7's one length test, `strlen(target) >
//     MAXN-DIRSIZ-2', checked the target but not the component being appended to it, and
//     dname() returns a pointer into argv where nothing bounds a component at DIRSIZ.  Each
//     is now checked against the sum it actually builds.  (That test's own arithmetic changed
//     under it, from 84 to 80, because DIRSIZ is 18 here and not v7's 14.  DIRSIZ comes from
//     <sys/param.h>, which is where it lives; task C24 dropped the <sys/dir.h> this file also
//     included, mv being on that task's list by mistake -- it reads no directory.)
//
// THREE THINGS THAT LOOK LIKE BUGS HERE AND ARE NOT.  Noted so the next reader does not
// "fix" them:
//
//  * pname() RETURNS A STATIC BUFFER, and several sites call it twice in one expression --
//    `stat(pname(source), &s1) || stat(pname(target), &s2)' and the two error messages in the
//    recovery arms.  Every one of them is sequenced, or wants the same value twice, so no
//    site holds two live results at once.  It works; it is fragile; do not extend it.
//
//  * THE CROSS-DEVICE PATH IS UNREACHABLE ON THIS MACHINE.  move() execs /bin/cp when
//    link() fails, which for a rename across filesystems means EXDEV -- and there is only one
//    filesystem here, one EC-5052 being the whole store and swap living on the drums.  The
//    path is ported faithfully and /bin/cp really is on the image (task C1b sequenced it
//    first for this reason), but nothing exercises it and nothing can until there is a second
//    thing to mount.
//
//  * mvdir() TAKES TWO DIFFERENT PATHS and the difference is `s1.st_ino != s2.st_ino', the
//    two parents.  Same parent: link, unlink, done -- `..' still points where it should.
//    Different parents: the four-call re-parent below, which is the only code in this task
//    whose failure is silent, since a wrong link count reads back perfectly until an fsck.
//    kernel/test/files is what puts b6fsutil -c behind it.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No long and no %D.
//
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DOT      "."
#define DOTDOT   ".."
#define DELIM    '/'
#define SDELIM   "/"
#define MAXN     100
#define MODEBITS 07777

static struct stat s1, s2;

static int move(char *source, char *target);
static int mvdir(char *source, char *target);
static char *pname(char *name);
static char *dname(char *name);
static int check(char *spth, ino_t dinode);
static int chkdot(char *s);

int main(int argc, char **argv)
{
    int i, r;

    if (argc < 3)
        goto usage;
    if (stat(argv[1], &s1) < 0) {
        fprintf(stderr, "mv: cannot access %s\n", argv[1]);
        return 1;
    }
    if ((s1.st_mode & S_IFMT) == S_IFDIR) {
        if (argc != 3)
            goto usage;
        // Renaming a directory needs the borrowed root, so this returns BEFORE the
        // setuid() below.  That order is v7's and is the whole of mv's privilege policy.
        return mvdir(argv[1], argv[2]);
    }
    setuid(getuid());
    if (argc > 3)
        if (stat(argv[argc - 1], &s2) < 0 || (s2.st_mode & S_IFMT) != S_IFDIR)
            goto usage;
    r = 0;
    for (i = 1; i < argc - 1; i++)
        r |= move(argv[i], argv[argc - 1]);
    return r;
usage:
    fprintf(stderr, "usage: mv f1 f2; or mv d1 d2; or mv f1 ... fn d1\n");
    return 1;
}

//
// Move one non-directory.  A link and an unlink where that works; a copy through /bin/cp
// where it does not, which on this machine is nowhere -- see the head of the file.
//
static int move(char *source, char *target)
{
    int c, i;
    int status;
    char buf[MAXN];

    if (stat(source, &s1) < 0) {
        fprintf(stderr, "mv: cannot access %s\n", source);
        return 1;
    }
    if ((s1.st_mode & S_IFMT) == S_IFDIR) {
        fprintf(stderr, "mv: directory rename only\n");
        return 1;
    }
    if (stat(target, &s2) >= 0) {
        if ((s2.st_mode & S_IFMT) == S_IFDIR) {
            // The bound v7 had not: buf grows to target + '/' + the last component of
            // source + a NUL, and both come from argv.
            if ((int)strlen(target) + (int)strlen(dname(source)) + 2 > (int)sizeof buf) {
                fprintf(stderr, "mv: %s/%s: name too long\n", target, dname(source));
                return 1;
            }
            sprintf(buf, "%s/%s", target, dname(source));
            target = buf;
        }
        if (stat(target, &s2) >= 0) {
            if ((s2.st_mode & S_IFMT) == S_IFDIR) {
                fprintf(stderr, "mv: %s is a directory\n", target);
                return 1;
            }
            if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) {
                fprintf(stderr, "mv: %s and %s are identical\n", source, target);
                return 1;
            }
            if (access(target, W_OK) < 0 && isatty(fileno(stdin))) {
                fprintf(stderr, "mv: %s: %o mode ", target, s2.st_mode & MODEBITS);
                i = c = getchar();
                while (c != '\n' && c != EOF)
                    c = getchar();
                if (i != 'y')
                    return 1;
            }
            if (unlink(target) < 0) {
                fprintf(stderr, "mv: cannot unlink %s\n", target);
                return 1;
            }
        }
    }
    if (link(source, target) < 0) {
        i = fork();
        if (i == -1) {
            fprintf(stderr, "mv: try again\n");
            return 1;
        }
        if (i == 0) {
            execl("/bin/cp", "cp", source, target, (char *)0);
            fprintf(stderr, "mv: cannot exec cp\n");
            exit(1);
        }
        status = 0;
        while ((c = wait(&status)) != i && c != -1)
            ;
        if (c == -1) {
            fprintf(stderr, "mv: cannot wait for cp\n");
            return 1;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return 1;

        // cp does not preserve times; mv does.  The array is explicit rather than v7's
        // `&s1.st_atime' -- see change 3 at the head of the file.
        {
            time_t tv[2];

            tv[0] = s1.st_atime;
            tv[1] = s1.st_mtime;
            utime(target, tv);
        }
    }
    if (unlink(source) < 0) {
        fprintf(stderr, "mv: cannot unlink %s\n", source);
        return 1;
    }
    return 0;
}

//
// Rename a directory.  Everything up to the signal() loop is validation; everything after it
// is the sequence that must not be interrupted.
//
static int mvdir(char *source, char *target)
{
    char *p;
    int i;
    char buf[MAXN];

    if (stat(target, &s2) >= 0) {
        if ((s2.st_mode & S_IFMT) != S_IFDIR) {
            fprintf(stderr, "mv: %s exists\n", target);
            return 1;
        }
        // The target is a directory, so the new name goes inside it.  v7 checked only
        // strlen(target) here (against MAXN-DIRSIZ-2, which is 80 rather than its 84
        // because DIRSIZ is 18); the component appended comes from argv and is bounded by
        // nothing, so check the sum that is actually built.
        if ((int)strlen(target) + (int)strlen(dname(source)) + 2 > (int)sizeof buf) {
            fprintf(stderr, "mv: target name too long\n");
            return 1;
        }
        strcpy(buf, target);
        strcat(buf, SDELIM);
        strcat(buf, dname(source));
        target = buf;
        if (stat(target, &s2) >= 0) {
            fprintf(stderr, "mv: %s exists\n", buf);
            return 1;
        }
    }
    if (strcmp(source, target) == 0) {
        fprintf(stderr, "mv: ?? source == target, source exists and target doesnt\n");
        return 1;
    }
    p = dname(source);
    if (!strcmp(p, DOT) || !strcmp(p, DOTDOT) || !strcmp(p, "") || p[strlen(p) - 1] == '/') {
        fprintf(stderr, "mv: cannot rename %s\n", p);
        return 1;
    }
    if (stat(pname(source), &s1) < 0 || stat(pname(target), &s2) < 0) {
        fprintf(stderr, "mv: cannot locate parent\n");
        return 1;
    }
    if (access(pname(target), W_OK) < 0) {
        fprintf(stderr, "mv: no write access to %s\n", pname(target));
        return 1;
    }
    if (access(pname(source), W_OK) < 0) {
        fprintf(stderr, "mv: no write access to %s\n", pname(source));
        return 1;
    }
    if (access(source, W_OK) < 0) {
        fprintf(stderr, "mv: no write access to %s\n", source);
        return 1;
    }
    if (s1.st_dev != s2.st_dev) {
        fprintf(stderr, "mv: cannot move directories across devices\n");
        return 1;
    }

    // The two parents differ, so the directory changes parent and its `..' must follow it.
    if (s1.st_ino != s2.st_ino) {
        char dst[MAXN + 5];

        if (chkdot(source) || chkdot(target)) {
            fprintf(stderr, "mv: Sorry, path names including %s aren't allowed\n", DOTDOT);
            return 1;
        }
        stat(source, &s1);
        if (check(pname(target), s1.st_ino))
            return 1;

        // "target/..", built and bounded HERE rather than in the middle of the sequence
        // below, where v7 built it -- out of an uninitialized buffer at that.  See change 1
        // at the head of the file.
        if ((int)strlen(target) + (int)sizeof(SDELIM DOTDOT) > (int)sizeof dst) {
            fprintf(stderr, "mv: %s: name too long\n", target);
            return 1;
        }
        strcpy(dst, target);
        strcat(dst, SDELIM);
        strcat(dst, DOTDOT);

        // Nothing below may be interrupted: between the link and the last of the three
        // calls that follow it, the tree is inconsistent.  Signal numbers run 1..NSIG-1.
        for (i = 1; i < NSIG; i++)
            signal(i, SIG_IGN);

        if (link(source, target) < 0) {
            fprintf(stderr, "mv: cannot link %s to %s\n", target, source);
            return 1;
        }
        if (unlink(source) < 0) {
            fprintf(stderr, "mv: %s: cannot unlink\n", source);
            unlink(target);
            return 1;
        }
        if (unlink(dst) < 0) {
            fprintf(stderr, "mv: %s: cannot unlink\n", dst);
            if (link(target, source) >= 0)
                unlink(target);
            return 1;
        }
        if (link(pname(target), dst) < 0) {
            fprintf(stderr, "mv: cannot link %s to %s\n", dst, pname(target));
            if (link(pname(source), dst) >= 0)
                if (link(target, source) >= 0)
                    unlink(target);
            return 1;
        }
        return 0;
    }

    // Same parent: `..' already points where it should, so a link and an unlink are all.
    if (link(source, target) < 0) {
        fprintf(stderr, "mv: cannot link %s and %s\n", source, target);
        return 1;
    }
    if (unlink(source) < 0) {
        fprintf(stderr, "mv: ?? cannot unlink %s\n", source);
        return 1;
    }
    return 0;
}

//
// The directory part of a path, in a static buffer -- see the head of the file on that.
//
static char *pname(char *name)
{
    int c;
    char *p, *q;
    static char buf[MAXN];

    p = q = buf;
    while ((c = *p++ = *name++))
        if (c == DELIM)
            q = p - 1;
    if (q == buf && *q == DELIM)
        q++;
    *q = 0;
    return buf[0] ? buf : DOT;
}

//
// The last component of a path.  Returns a pointer INTO its argument, so nothing here bounds
// a component at DIRSIZ -- which is why every caller checks its own buffer.
//
static char *dname(char *name)
{
    char *p;

    p = name;
    while (*p)
        if (*p++ == DELIM && *p)
            name = p;
    return name;
}

//
// Refuse to move a directory into itself: walk from the target's parent up to the root
// through `..', and fail if the source's i-number is met on the way.
//
static int check(char *spth, ino_t dinode)
{
    char nspth[MAXN];
    struct stat sbuf;

    sbuf.st_ino = 0;

    if ((int)strlen(spth) >= (int)sizeof nspth) {
        fprintf(stderr, "mv: name too long\n");
        return 1;
    }
    strcpy(nspth, spth);
    while (sbuf.st_ino != ROOTINO) {
        if (stat(nspth, &sbuf) < 0) {
            fprintf(stderr, "mv: cannot access %s\n", nspth);
            return 1;
        }
        if (sbuf.st_ino == dinode) {
            fprintf(stderr, "mv: cannot move a directory into itself\n");
            return 1;
        }
        if ((int)strlen(nspth) > (int)(MAXN - 2 - sizeof(DOTDOT))) {
            fprintf(stderr, "mv: name too long\n");
            return 1;
        }
        strcat(nspth, SDELIM);
        strcat(nspth, DOTDOT);
    }
    return 0;
}

//
// Does any component of this path spell `..'?  mvdir() refuses such a path outright rather
// than reason about where it lands.
//
static int chkdot(char *s)
{
    do {
        if (strcmp(dname(s), DOTDOT) == 0)
            return 1;
        s = pname(s);
    } while (strcmp(s, DOT) != 0 && strcmp(s, SDELIM) != 0);
    return 0;
}
