//
// suidt -- the set-user-id bit: that it is on the image, and that exec honours it.
//
// THIS PROGRAM RUNS ON THE DISK IMAGE ONLY, never under b6sim, and like memt.c next door it
// is not a libc test at all.  It belongs to cmd/mkdir and cmd/rmdir (task C1a) and to cmd/mv
// (task C1b) -- the three setuid-root programs this system has -- and it is here for the
// reason memt is: /usr/test is where a program can be run off the image by a booted kernel
// for the price of one b6_libtest() call.  b6sim could not run it in any case -- there is no
// /bin/mkdir on the build machine, and no kernel underneath to change a uid.
//
// WHY IT HAS TO EXIST.  getxfile() honours ISUID only for a caller that is not already root:
//
//      if (ip->i_mode & ISUID)
//          if (u.u_uid != 0) { u.u_uid = ip->i_uid; ... }      -- kernel/sys1.c
//
// and every shell on this machine is root's -- init execs /bin/sh directly, there being no
// getty and no login.  So `mkdir' typed at the console prompt proves that mkdir works; it
// proves nothing whatever about the setuid bit, because the branch is not taken.  Only a
// process that has dropped privilege can take it, and this program is the only thing on the
// image that makes one.
//
// THE PROOF, and it is one call rather than two.  A child drops to uid 7 (guest, /etc/passwd)
// and execs /bin/mkdir.  Then:
//
//   * the directory EXISTING proves the effective uid became 0 -- mknod() and link()-on-a-
//     directory are gated on suser() (kernel/sys2.c), so a real uid-7 process cannot make
//     one at all;
//   * its being owned by 7 proves the REAL uid stayed 7 -- maknode() gives a new inode
//     u.u_uid, the EFFECTIVE one, so the directory is born root's, and mkdir then chowns it
//     to getuid(), which is u_ruid (lib/libc/sys/getuid.S returns both, real in the
//     accumulator).
//
// Neither half is worth much without the other: a root child would satisfy the first, and a
// kernel that ignored ISUID would fail it.
//
// AND THE SAME ARGUMENT FOR mv, which task C1b made the third setuid program (cmd/mv/README.md
// is the account).  Renaming a directory is link() and unlink() on a directory, both
// suser()-only, so the same one call proves both halves again: the new name existing proves
// the effective uid became 0, and its still being owned by 7 proves the real uid never was.
// mv is exercised on BOTH of mvdir()'s branches, because they are different code and only one
// of them is dangerous:
//
//   * a rename inside one parent is a link and an unlink, and `..' still points where it
//     should;
//   * a move to a DIFFERENT parent is four calls -- link, unlink, unlink target/.., link
//     parent target/.. -- and a link count it gets wrong reads back perfectly until an fsck.
//
// The link counts of both parents are asserted here for that reason.  kernel/test/files is
// the same path run as root with b6fsutil -c behind it; this is the same path run by a
// process that has no privilege of its own.
//
// AND THE NEGATIVE CONTROL, in a child of the same uid that execs nothing: mknod(), link()-of-
// a-directory and unlink()-of-a-directory must all come back EPERM.  Without it, "the
// directory appeared" would be consistent with a kernel that let anybody call mknod, and "the
// directory moved" with one that let anybody link a directory.
//
// WHAT MAY REACH THE .expected FILE: verdicts only, as in memt.  No uid, no mode, no
// i-number, no pid -- every number this program learns is compared against one it already
// knows, and the modes it expects are reproducible only because CMASK is 0 (<sys/param.h>),
// so maknode()'s ~u_cmask takes nothing off the 0777 mkdir asks for.  Give the shell a umask
// builtin and this expectation is one of the things that changes.
//
// THE HOST SIDE OF THE SAME QUESTION is ctest rootimg_setuid (kernel/test/CMakeLists.txt),
// which reads 4755 back off root.img with b6fsutil.  Between them the two failure modes are
// separated: b6fsutil dropping the bit when it writes the inode, and getxfile() ignoring it
// when it execs.  This program is the second.
//
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// guest, out of /etc/passwd: uid 7, gid 3.  The only non-root identity on this image, and
// nothing else on it assumes anything about that account.
#define NOBODY 7
#define NOGRP  3

#define DIR   "/tmp/suidt.d"  // what the setuid mkdir is asked to make
#define DIR2  "/tmp/suidt.e"  // ...what the setuid mv renames it to, in the same parent
#define SUB   DIR2 "/sub"     // ...a directory inside that one
#define MOVED "/tmp/suidt.s"  // ...and where mv re-parents it to, which is the hard branch
#define NOPE  "/tmp/suidt.nc" // what the negative control is refused
#define NOLNK "/tmp/suidt.nl" // ...and the link it is refused

// The bits the negative-control child reports through its exit status.
#define NC_MKNOD  1 // mknod() as uid 7 was refused with EPERM
#define NC_UNLINK 2 // unlink() of a directory as uid 7 was refused with EPERM
#define NC_SETUID 4 // setuid(0) after setuid(7) was refused -- the drop is irreversible
#define NC_LINK   8 // link() of a directory as uid 7 was refused with EPERM

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

// Become guest.  ORDER MATTERS: setgid() needs suser() while u_rgid is still 0, and
// setuid() sets u_ruid as well as u_uid (kernel/sys4.c), after which suser() answers no.
// Returns 0 on success; the caller is a child and has nowhere to report but its status.
static int becomeguest(void)
{
    if (setgid(NOGRP) < 0)
        return -1;
    if (setuid(NOBODY) < 0)
        return -1;
    return 0;
}

// Run one command as guest and hand back its wait status, or -1 if the fork or the drop
// failed.  A null `arg2' makes it a one-argument command, which is what mkdir and rmdir take
// and mv does not.  stdout is a file here (libtest.sh redirects it), so it must be flushed
// before the fork or the child would inherit a copy of the buffer.
static int rundropped(const char *path, const char *arg, const char *arg2)
{
    int pid, status;

    fflush(stdout);
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (becomeguest() < 0)
            _exit(126);
        if (arg2 == NULL)
            execl(path, path, arg, (char *)0);
        else
            execl(path, path, arg, arg2, (char *)0);
        _exit(127); // the exec failed -- no such program, or no permission to run it
    }
    while (wait(&status) != pid)
        ;
    return status;
}

// Did that child exec the setuid program and come back with a zero status?
static int ranok(int status)
{
    return status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
    struct stat st;
    int status, pid, bits, tmpnlink;

    // The precondition every assertion below rests on.  If the harness ever runs this
    // program as anything but root, the setuid branch would be taken by the PARENT too and
    // the children would prove nothing.
    ok("we start as root", getuid() == 0 && geteuid() == 0);

    // The bit itself, read off the inode this kernel is about to exec.  root.manifest is
    // where it comes from -- `mode 04755' -- and nothing in build/rootfs/ carries it.
    ok("/bin/mkdir is setuid root",
       stat("/bin/mkdir", &st) == 0 && (st.st_mode & 07777) == 04755 && st.st_uid == 0);
    ok("/bin/mv is setuid root",
       stat("/bin/mv", &st) == 0 && (st.st_mode & 07777) == 04755 && st.st_uid == 0);
    ok("/bin/rmdir is setuid root",
       stat("/bin/rmdir", &st) == 0 && (st.st_mode & 07777) == 04755 && st.st_uid == 0);

    // The parent directory's link count, as a baseline.  Never asserted absolutely: /tmp
    // is shared with every other program in this suite.
    ok("/tmp is a directory", stat("/tmp", &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR);
    tmpnlink = st.st_nlink;

    // THE TRANSITION.  A uid-7 process execs the setuid mkdir.
    status = rundropped("/bin/mkdir", DIR, NULL);
    ok("guest ran /bin/mkdir to a normal exit", ranok(status));

    if (stat(DIR, &st) < 0) {
        ok("the directory exists, so the effective uid became root", 0);
        return 1;
    }
    ok("the directory exists, so the effective uid became root", (st.st_mode & S_IFMT) == S_IFDIR);
    ok("it is owned by guest, so the real uid stayed guest's", st.st_uid == NOBODY);
    ok("and carries guest's group", st.st_gid == NOGRP);
    ok("its mode is 0777", (st.st_mode & 07777) == 0777);
    ok("its link count is 2, so it has both . and ..", st.st_nlink == 2);
    ok("and the parent gained a link", stat("/tmp", &st) == 0 && st.st_nlink == tmpnlink + 1);

    // THE NEGATIVE CONTROL, in a child that execs nothing and so keeps uid 7 throughout.
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        int b = 0;
        if (becomeguest() < 0)
            _exit(0); // no bits set: the parent reports all three as failures
        if (mknod(NOPE, 040777, 0) < 0 && errno == EPERM)
            b |= NC_MKNOD;
        if (unlink(DIR "/..") < 0 && errno == EPERM)
            b |= NC_UNLINK;
        if (link(DIR, NOLNK) < 0 && errno == EPERM)
            b |= NC_LINK;
        if (setuid(0) < 0)
            b |= NC_SETUID;
        _exit(b);
    }
    while (wait(&status) != pid)
        ;
    bits = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    ok("mknod as guest is refused", (bits & NC_MKNOD) != 0);
    ok("unlink of a directory as guest is refused", (bits & NC_UNLINK) != 0);
    ok("link to a directory as guest is refused", (bits & NC_LINK) != 0);
    ok("and guest cannot climb back to root", (bits & NC_SETUID) != 0);
    ok("so nothing was created behind the refusals", stat(NOPE, &st) < 0 && stat(NOLNK, &st) < 0);

    // THE THIRD SETUID PROGRAM, on the easier of mvdir()'s two branches: a rename inside one
    // parent, which is a link and an unlink and leaves `..' alone.  /tmp's link count must
    // therefore NOT move.
    status = rundropped("/bin/mv", DIR, DIR2);
    ok("guest ran /bin/mv to a normal exit", ranok(status));
    ok("the renamed directory exists, so the effective uid became root again",
       stat(DIR2, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR);
    ok("it is still owned by guest, so mv never granted the real uid", st.st_uid == NOBODY);
    ok("its link count is still 2", st.st_nlink == 2);
    ok("the old name is gone", stat(DIR, &st) < 0);
    ok("and a rename within one parent left that parent's link count alone",
       stat("/tmp", &st) == 0 && st.st_nlink == tmpnlink + 1);

    // ...and on the branch that matters: a move to a DIFFERENT parent, which is four calls
    // and is the only place in this task where a mistake is silent.  Both parents' link
    // counts are the assertion -- the old one must give up the `..' the new one gains.
    status = rundropped("/bin/mkdir", SUB, NULL);
    ok("guest made a subdirectory to move", ranok(status));
    ok("so its parent gained a link", stat(DIR2, &st) == 0 && st.st_nlink == 3);

    status = rundropped("/bin/mv", SUB, MOVED);
    ok("guest ran /bin/mv across parents to a normal exit", ranok(status));
    ok("the moved directory exists", stat(MOVED, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR);
    ok("it is still owned by guest", st.st_uid == NOBODY);
    ok("and still has both . and ..", st.st_nlink == 2);
    ok("the old name is gone", stat(SUB, &st) < 0);
    ok("the old parent gave up its link", stat(DIR2, &st) == 0 && st.st_nlink == 2);
    ok("and the new parent took it", stat("/tmp", &st) == 0 && st.st_nlink == tmpnlink + 2);

    // And back the other way, through the other setuid program.
    status = rundropped("/bin/rmdir", MOVED, NULL);
    ok("guest ran /bin/rmdir on the moved directory", ranok(status));
    status = rundropped("/bin/rmdir", DIR2, NULL);
    ok("guest ran /bin/rmdir on the renamed one", ranok(status));
    ok("both directories are gone", stat(MOVED, &st) < 0 && stat(DIR2, &st) < 0);
    ok("and the parent has its link count back", stat("/tmp", &st) == 0 && st.st_nlink == tmpnlink);

    return 0;
}
