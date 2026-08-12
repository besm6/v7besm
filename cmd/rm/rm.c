/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// rm -- remove (unlink) files.
//
// The v7 program, unchanged in what it does: one clumped flag word (-f, -i, -r), a refusal of
// the literal argument `..', then unlink() per file -- and under -r a recursive descent that
// reads each directory and hands the emptied one to /bin/rmdir.
//
// THAT LAST STEP IS WHY rm ITSELF NEEDS NO PRIVILEGE.  unlink() of a directory is gated on
// suser() (kernel/sys4.c), and rm never calls it: removedir() below forks and execs
// /bin/rmdir, which IS set-user-id root on the image (`mode 04755' in ../../scripts/root.manifest).
// So `rm -r' works for an ordinary user without rm being setuid, which is v7's arrangement and
// is kept.  ../mkdir/README.md is the setuid account.
//
// The C11 pass is the usual one (../init/README.md is the worked example): prototypes and
// explicit return types everywhere, `static' on errcode and on all four functions, the
// pre-ANSI `char *sprintf();' deleted -- it was also WRONG, <stdio.h> having declared sprintf
// as returning int since ANSI -- open(arg, 0) spelled O_RDONLY, access(arg, 02) spelled W_OK,
// and the missing <fcntl.h>, <stdlib.h>, <string.h>, <sys/wait.h> and <unistd.h> added.  The
// helper is `removedir' rather than v7's `rmdir', for the reason ../mkdir/mkdir.c gives about
// `makedir'.  `errcode' also moved below the includes, where a definition belongs.
//
// FOUR CHANGES BEYOND THE MECHANICAL PASS:
//
//  1. `%.14s' TRUNCATED A NAME.  DIRSIZ is 18 here, not v7's 14 (include/sys/param.h), so
//     `rm -r' silently built a short name for any entry of 15 to 18 characters, and then
//     removed the wrong file or -- more usually -- reported the right one nonexistent and left
//     the directory un-removable.  readdir() hands back a terminated name and %s prints it.
//
//  2. THE RECURSIVE sprintf() WAS UNBOUNDED, and that is the sharp one: rm() recurses on the
//     name it builds, so the path grows one component per level and the sprintf has no idea
//     how deep it is.  ../mkdir/mkdir.c and ../rmdir/rmdir.c each bounded their equivalent for
//     the same reason -- the 4,096-word stack at 070000 is the one address-space ceiling
//     nothing checks (../README.md, the porting recipe) -- and neither of those recursed.
//
//  3. A FAILED rmdir MADE rm EXIT SUCCESSFULLY.  `errcode += rmdir(arg, iflg)' added a raw
//     wait(2) status word to an exit code, and a normal exit stores its code in the HIGH byte
//     (`(rval & 0377) << 8', kernel/sys1.c).  So one failed /bin/rmdir contributed 0400 to
//     errcode, and exit(errcode) kept the low eight bits: zero.  Success.  <sys/wait.h>'s
//     WIFEXITED/WEXITSTATUS say what was meant, and removedir() now returns a plain count.
//
//  4. THE -i PROMPTS NEVER APPEARED.  Every one of them is written to stdout without a
//     newline and immediately followed by getchar(), and this libc line-buffers stdout to a
//     terminal where v7's left it unbuffered (lib/libc/README.md, lib/libc/stdio/data.c).  So
//     `rm -i' asked its question into the buffer and read a blind terminal.  yes() flushes
//     before it reads, which covers all four prompt sites at once.
//
// THE DESCENT READS EACH DIRECTORY WITH opendir(3), task C24, and keeps the DIR OPEN ACROSS
// THE RECURSION as v7 kept the descriptor -- so the depth limit is still NOFILE, 20
// (include/sys/param.h), and rm.1.umm still says so.  What a DIR adds is heap: twelve words
// plus a read buffer per level.  README.md measures it and says why closing before descending
// was not the answer.
//
// ONE THING THAT LOOKS LIKE A BUG HERE AND IS NOT.  The second execl(), of /usr/bin/rmdir, is
// dead: there is no /usr/bin on this image.  It is v7's and costs one failed exec on a path
// that has already failed once, so it stays.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No long and no %D.
//
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// v7's own 100.  Kept rather than grown: it is 17 words per level of recursion, against the
// 4,096-word stack, and the bound in rm() is what makes the number mean anything.
#define NAMEBUF 100

static int errcode = 0;

static void rm(char *arg, int fflg, int rflg, int iflg, int level);
static int dotname(char *s);
static int removedir(char *f, int iflg);
static int yes(void);

int main(int argc, char **argv)
{
    char *arg;
    int fflg, iflg, rflg;

    fflg = 0;
    if (isatty(0) == 0)
        fflg++; // no terminal to ask a question of
    iflg = 0;
    rflg = 0;
    if (argc > 1 && argv[1][0] == '-') {
        arg = *++argv;
        argc--;
        while (*++arg != '\0')
            switch (*arg) {
            case 'f':
                fflg++;
                break;
            case 'i':
                iflg++;
                break;
            case 'r':
                rflg++;
                break;
            default:
                printf("rm: unknown option %s\n", *argv);
                exit(1);
            }
    }
    while (--argc > 0) {
        if (!strcmp(*++argv, "..")) {
            fprintf(stderr, "rm: cannot remove `..'\n");
            continue;
        }
        rm(*argv, fflg, rflg, iflg, 0);
    }

    return errcode;
}

static void rm(char *arg, int fflg, int rflg, int iflg, int level)
{
    struct stat buf;
    char name[NAMEBUF];
    DIR *dirp;
    struct dirent *dp;

    if (stat(arg, &buf)) {
        if (fflg == 0) {
            printf("rm: %s nonexistent\n", arg);
            ++errcode;
        }
        return;
    }
    if ((buf.st_mode & S_IFMT) == S_IFDIR) {
        if (rflg) {
            if (access(arg, W_OK) < 0) {
                if (fflg == 0)
                    printf("%s not changed\n", arg);
                errcode++;
                return;
            }
            if (iflg && level != 0) {
                printf("directory %s: ", arg);
                if (!yes())
                    return;
            }

            // The bound v7 had not, and this function recurses on what it builds: the
            // longest name it can make here is arg + '/' + DIRSIZ characters + a NUL.
            if ((int)strlen(arg) + DIRSIZ + 2 > (int)sizeof name) {
                printf("rm: %s: name too long\n", arg);
                ++errcode;
                return;
            }
            if ((dirp = opendir(arg)) == NULL) {
                printf("rm: %s: cannot read\n", arg);
                exit(1);
            }
            while ((dp = readdir(dirp)) != NULL) {
                if (!dotname(dp->d_name)) {
                    sprintf(name, "%s/%s", arg, dp->d_name);
                    rm(name, fflg, rflg, iflg, level + 1);
                }
            }
            closedir(dirp);
            errcode += removedir(arg, iflg);
            return;
        }
        printf("rm: %s directory\n", arg);
        ++errcode;
        return;
    }

    if (iflg) {
        printf("%s: ", arg);
        if (!yes())
            return;
    } else if (!fflg) {
        if (access(arg, W_OK) < 0) {
            printf("rm: %s %o mode ", arg, buf.st_mode & 0777);
            if (!yes())
                return;
        }
    }
    if (unlink(arg) && (fflg == 0 || iflg)) {
        printf("rm: %s not removed\n", arg);
        ++errcode;
    }
}

static int dotname(char *s)
{
    if (s[0] == '.') {
        if (s[1] == '.') {
            if (s[2] == '\0')
                return 1;
            else
                return 0;
        } else if (s[1] == '\0')
            return 1;
    }
    return 0;
}

//
// Remove one emptied directory, through /bin/rmdir -- which is setuid root, unlink() of a
// directory being suser()-only.  Returns an error COUNT, not a wait(2) status; see change 3
// at the head of the file.
//
static int removedir(char *f, int iflg)
{
    int status, i, c;

    if (dotname(f))
        return 0;
    if (iflg) {
        printf("%s: ", f);
        if (!yes())
            return 0;
    }
    fflush(stdout); // the child inherits this buffer; do not let it print twice
    while ((i = fork()) == -1)
        sleep(3);
    if (i) {
        status = 0;
        while ((c = wait(&status)) != i && c != -1)
            ;
        if (c == -1)
            return 1;
        return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
    }
    execl("/bin/rmdir", "rmdir", f, (char *)0);
    execl("/usr/bin/rmdir", "rmdir", f, (char *)0); // dead here; v7's, and harmless
    printf("rm: can't find rmdir\n");
    exit(1);
}

//
// Read a line and answer whether it began with `y'.  The flush is not v7's: stdout is line
// buffered to a terminal here, and every prompt this program writes lacks a newline.
//
static int yes(void)
{
    int i, b;

    fflush(stdout);
    i = b = getchar();
    while (b != '\n' && b != EOF)
        b = getchar();
    return i == 'y';
}
