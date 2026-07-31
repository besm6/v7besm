/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// mkdir -- make a directory.
//
// The v7 program, unchanged in what it does.  There is no mkdir(2) on this system, as there
// was none on v7: a directory is built by hand out of three unprivileged-looking calls that
// are anything but --
//
//     mknod(d, 040777, 0)   creates the empty directory inode
//     link(d, "d/.")        gives it its own `.'
//     link(pname, "d/..")   gives it its parent
//
// and all three are gated on suser() in this kernel (mknod in kernel/sys2.c, link's
// directory arm beside it), which is why /bin/mkdir is SET-USER-ID ROOT on the image --
// `mode 04755' in ../../root.manifest, the first setuid entry there.  cmd/mkdir/README.md is
// the account of what that costs and how it is asserted; the short of it is that the program
// itself makes the only permission check that matters, `access(pname, W_OK)' on the parent,
// and access(2) asks about the REAL uid.  So the caller must already be able to write the
// parent directory, and root is borrowed for nothing but the three calls above.
//
// The C11 pass is the usual one -- a prototype and an explicit return type on main(), `static'
// on everything file-local, `register i, slash = 0;' (untyped, so implicitly int) spelled out,
// and the pre-ANSI `char *strcat();' re-declarations deleted in favour of <string.h>.  The
// file-scope helper is `makedir' rather than v7's `mkdir': <sys/stat.h> here already declares
// mknod/chmod/stat and is one line away from declaring mkdir too, and cmd/ls renamed its
// readdir() and select() for exactly that reason.
//
// TWO CHANGES BEYOND THE MECHANICAL PASS, both marked below:
//
//  1. `dname[strlen(dname)] = '\0';' on the second link's failure path is a NO-OP -- it
//     overwrites the terminator with itself.  The intent was `- 1', stripping the trailing
//     dot of "d/.." back to "d/." so that the `.' link made a moment earlier is the one
//     unlinked.  As v7 wrote it the cleanup unlinks "d/..", a name that by construction does
//     not exist (its link is the one that just failed), so `d' is left an allocated directory
//     inode with nlink 1, no entry in any parent, and a `.' pointing at itself -- exactly what
//     b6fsutil -c reports in its third pass.  Fixed.  Nothing tests it: reaching it needs
//     link(2) to fail, which on this one-filesystem machine means a parent with no room.
//
//  2. Neither buffer was bounded.  v7 copies an argv string into char[128] with strcpy() and
//     then strcat()s onto it, and on this machine that walks off the frame -- and the 4,096
//     words of stack at 070000 are the one ceiling nothing checks (cmd/README.md section 6).
//     That was harmless when nothing could reach mkdir but the person typing it; it is not
//     harmless in a program that runs with an effective uid of 0.  One length test now covers
//     both buffers, `d' plus "/.." plus the NUL being the longest thing either holds.
//
// Nothing else moved.  There is no `long' here, no %D and no DIRSIZ assumption.
//
// THE MODE IS 0777 MODIFIED BY umask(2).  maknode() applies ~u.u_cmask, and CMASK is 0 in
// <sys/param.h>, so a directory made here is 0777 today.  Whoever gives the shell a umask
// builtin changes that, and the expectation files that name a mode with it.
//
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// How long a directory name mkdir will take.  v7 wrote 128 twice; it is one constant now,
// and the bound below is what makes it mean something.
#define NAMEBUF 128

static int Errors = 0;

static void makedir(char *d);

int main(int argc, char **argv)
{
    // v7 ignores every signal a user can send from the terminal rather than risk being
    // killed between the mknod and the two links -- the window in which a directory exists
    // with no `.' and no `..' in it.  A handler is `void (*)(int)' here (include/README.md),
    // so these calls need no cast.
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if (argc < 2) {
        fprintf(stderr, "mkdir: arg count\n");
        exit(1);
    }
    while (--argc)
        makedir(*++argv);
    return Errors != 0;
}

static void makedir(char *d)
{
    char pname[NAMEBUF], dname[NAMEBUF];
    int i, slash = 0;

    // The bound v7 had not.  dname holds d + "/.." + a NUL, which is the longest string
    // either buffer takes; pname holds a prefix of d plus "." and is shorter.
    if ((int)strlen(d) + 4 > (int)sizeof dname) {
        fprintf(stderr, "mkdir: %s: name too long\n", d);
        ++Errors;
        return;
    }

    // pname is the parent: everything up to and including the last slash, then a dot -- so
    // `mkdir a/b/c' checks "a/b/." and a plain `mkdir c' checks ".".
    pname[0] = '\0';
    for (i = 0; d[i]; ++i)
        if (d[i] == '/')
            slash = i + 1;
    if (slash)
        strncpy(pname, d, slash);
    strcpy(pname + slash, ".");
    if (access(pname, W_OK)) {
        fprintf(stderr, "mkdir: cannot access %s\n", pname);
        ++Errors;
        return;
    }
    if (mknod(d, 040777, 0) < 0) {
        fprintf(stderr, "mkdir: cannot make directory %s\n", d);
        ++Errors;
        return;
    }

    // The new directory belongs to whoever asked for it, not to the root this program
    // borrowed: getuid()/getgid() are the REAL ids, and maknode() gave the inode the
    // effective ones.  That single call is also what makes the setuid transition visible
    // from outside -- see lib/test/suidt.c.
    chown(d, getuid(), getgid());

    strcpy(dname, d);
    strcat(dname, "/.");
    if (link(d, dname) < 0) {
        fprintf(stderr, "mkdir: cannot link %s\n", dname);
        unlink(d);
        ++Errors;
        return;
    }
    strcat(dname, ".");
    if (link(pname, dname) < 0) {
        fprintf(stderr, "mkdir: cannot link %s\n", dname);
        dname[strlen(dname) - 1] = '\0'; // "d/.." -> "d/." -- v7 wrote no -1; see above
        unlink(dname);
        unlink(d);
        ++Errors;
    }
}
