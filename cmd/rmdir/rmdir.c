/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// rmdir -- remove a directory.
//
// The v7 program, unchanged in what it does.  There is no rmdir(2) either, so a directory is
// taken apart the way mkdir(1) built it, by hand and backwards:
//
//     unlink("d/..")   give the parent its link count back
//     unlink("d/.")    and the directory its own
//     unlink("d")      and take the name out of the parent
//
// unlink() of a directory is gated on suser() (kernel/sys4.c), so all three need root and
// /bin/rmdir is SET-USER-ID ROOT on the image -- `mode 04755' in ../../scripts/root.manifest.  See
// ../mkdir/README.md for the setuid account; this file's own README covers what is peculiar
// to rmdir.  As in mkdir, the program makes its own permission check first, `access(name/..,
// W_OK)', which asks about the REAL uid, so root is borrowed and not granted.
//
// The C11 pass is the usual one -- a prototype and an explicit return type on main(), `static'
// on the file-local pair, the pre-ANSI `char *rindex();' re-declarations deleted.  The helper
// is `removedir' rather than v7's `rmdir', for the reason ../mkdir/mkdir.c gives about
// `makedir'.  `rindex' became strrchr(): rindex is in this libc but NO HEADER DECLARES IT
// (lib/libc/gen/index.c says why -- it is not ANSI and v7 has no <strings.h>), strrchr is in
// <string.h>, and lib/libc/gen/ttyslot.c already uses it for this same "last component of a
// path" job.  The bare 0 and 02 handed to access() are F_OK and W_OK.
//
// TWO CHANGES BEYOND THE MECHANICAL PASS:
//
//  1. `np = rindex(name, '/')' leaves np pointing AT the slash -- v7 has no `else np++' -- so
//     the guard that refuses `.' and `..' compares "/." against "." and never fires for a
//     qualified name.  `rmdir foo/.' therefore runs the whole unlink sequence against
//     "foo/./..", "foo/./." and "foo/.", which unlinks the PARENT's entry and foo's own,
//     decrementing two link counts that nothing puts back, and then fails on the third unlink
//     and reports `not removed' -- leaving foo still linked into its parent with a link count
//     that is now wrong.  This one corrupts, and kernel/test/session.sh guards against it.
//
//  2. `strcpy(name, d)' from argv into char[500] was unbounded, and three strcat()s follow
//     it.  Bounded now, for the reason ../mkdir/mkdir.c gives at greater length: the stack is
//     the one ceiling nothing checks, and this program runs with an effective uid of 0.
//
// THE EMPTINESS TEST IS opendir(3), task C24: readdir() skips the slot a removed entry leaves
// behind and terminates the name, so the loop says only what it means.  README.md has the two
// arguments that the hand-rolled read() needed and this does not.
//
// stat("") IS DELIBERATE, and it works on this kernel: namei() starts at u.u_cdir and only
// faults an empty path when its flag says create-or-delete (kernel/nami.c), which stat's does
// not -- so it names the current directory, which is how rmdir refuses to remove the
// directory it is standing in.
//
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// v7's own 500.  Kept rather than rounded: it is 84 words of the 4,096-word stack, and the
// bound in removedir() is what makes the number mean anything.
#define NAMEBUF 500

static int Errors = 0;

static void removedir(char *d);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "rmdir: arg count\n");
        exit(1);
    }
    while (--argc)
        removedir(*++argv);
    return Errors != 0;
}

static void removedir(char *d)
{
    DIR *dirp;
    struct dirent *dp;
    char *np, name[NAMEBUF];
    struct stat st, cst;

    // The bound v7 had not: name grows to d + "/.." + a NUL.
    if ((int)strlen(d) + 4 > (int)sizeof name) {
        fprintf(stderr, "rmdir: %s: name too long\n", d);
        ++Errors;
        return;
    }
    strcpy(name, d);

    // The last component, for the `.' and `..' refusal below.  The ++ is not v7's; see above.
    if ((np = strrchr(name, '/')) == NULL)
        np = name;
    else
        np++;

    if (stat(name, &st) < 0) {
        fprintf(stderr, "rmdir: %s non-existent\n", name);
        ++Errors;
        return;
    }
    if (stat("", &cst) < 0) {
        fprintf(stderr, "rmdir: cannot stat \"\"\n");
        ++Errors;
        exit(1);
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        fprintf(stderr, "rmdir: %s not a directory\n", name);
        ++Errors;
        return;
    }
    if (st.st_ino == cst.st_ino && st.st_dev == cst.st_dev) {
        fprintf(stderr, "rmdir: cannot remove current directory\n");
        ++Errors;
        return;
    }

    // Empty means nothing in it but `.' and `..'.
    if ((dirp = opendir(name)) == NULL) {
        fprintf(stderr, "rmdir: %s unreadable\n", name);
        ++Errors;
        return;
    }
    while ((dp = readdir(dirp)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;
        fprintf(stderr, "rmdir: %s not empty\n", name);
        ++Errors;
        closedir(dirp);
        return;
    }
    closedir(dirp);

    if (!strcmp(np, ".") || !strcmp(np, "..")) {
        fprintf(stderr, "rmdir: cannot remove . or ..\n");
        ++Errors;
        return;
    }

    // Write permission on the PARENT is what removing a directory takes, and "name/.." is
    // how this program spells it without parsing the path a second time.  The two access()
    // calls before it are there for a directory already half taken apart -- one whose `.'
    // or `..' a previous run removed -- which is repaired rather than refused.
    strcat(name, "/.");
    if (access(name, F_OK) < 0) { // name/. non-existent
        strcat(name, ".");
        goto unl;
    }
    strcat(name, ".");
    if (access(name, F_OK) < 0) // name/.. non-existent
        goto unl2;
    if (access(name, W_OK)) {
        name[strlen(name) - 3] = '\0';
        fprintf(stderr, "rmdir: %s: no permission\n", name);
        ++Errors;
        return;
    }
unl:
    unlink(name); // unlink name/..
unl2:
    name[strlen(name) - 1] = '\0';
    unlink(name); // unlink name/.
    name[strlen(name) - 2] = '\0';
    if (unlink(name) < 0) {
        fprintf(stderr, "rmdir: %s not removed\n", name);
        ++Errors;
    }
}
