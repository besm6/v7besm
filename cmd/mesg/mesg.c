/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// mesg -- set the current terminal to accept or forbid write permission.
//
//      mesg       report, and exit 0 for y and 1 for n
//      mesg y     chmod 0622 -- anybody may write(1) to this terminal
//      mesg n     chmod 0600 -- nobody may
//
// Task C6, and the smallest program in it: a stat() and a chmod() on ttyname(2).
//
// IT WORKS ONLY BECAUSE login CHOWNED THE TERMINAL FIRST.  chmod(2) here is gated on
// owner() (kernel/sys4.c), which admits the file's owner or the super-user and nobody
// else -- and a terminal node belongs to root until somebody logs in on it.
// cmd/login/login.c's chown() is what hands it over, before it drops privilege, and
// cmd/login/README.md calls that ordering load-bearing.  So `mesg n' typed at a shell
// init exec'd directly works only because that shell is root's; typed at a login shell it
// works because the node is now the user's.  There is nothing to make setuid here: the
// gate is ownership, and a setuid mesg would let anybody silence anybody.
//
// THE NODES SHIP AT 0622 (root.manifest), so a fresh boot answers `is y' and v7's two
// numbers are exactly the two states this system has.
//
// TWO FIXES, both marked `Note:' in mesg.1.umm:
//
//   - ttyname(2) RETURNS NULL for a process with no terminal -- a mesg in a shell script
//     with its standard error redirected -- and v7 handed that NULL straight to stat().
//     It is checked here.
//
//   - error() exited -1.  Task C2 established that an exit status above 127 does not
//     survive wait(2) on this machine, so -1 reaches the shell as 255 and says nothing
//     the manual claims; the failure status is 2, which is distinct from the 0 and 1 the
//     two reports use.
//
// THE REPORT GOES TO STANDARD ERROR, which is v7's and looks like a mistake until you
// notice that the descriptor it asks about is 2 as well.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static struct stat sbuf;
static char *tty;

static void error(const char *s)
{
    fprintf(stderr, "mesg: %s\n", s);
    exit(2);
}

static void newmode(int m)
{
    if (chmod(tty, m) < 0)
        error("cannot change mode");
}

int main(int argc, char *argv[])
{
    int r = 0;

    tty = ttyname(2);
    if (tty == NULL)
        error("not a tty");
    if (stat(tty, &sbuf) < 0)
        error("cannot stat");
    if (argc < 2) {
        if (sbuf.st_mode & 02) {
            fprintf(stderr, "is y\n");
        } else {
            r = 1;
            fprintf(stderr, "is n\n");
        }
    } else
        switch (*argv[1]) {
        case 'y':
            newmode(0622);
            break;

        case 'n':
            newmode(0600);
            r = 1;
            break;

        default:
            error("usage: mesg [y] [n]");
        }
    return r;
}
