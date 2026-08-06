//
// ttyt -- ttyname(3), ttyslot(3) and getlogin(3) when they have something to say.
//
// THE HALF pwent.c HAD TO GIVE UP.  Those three used to be asserted there, and could only be
// asserted for their FAILURE paths, because pwent runs in both worlds and adjudicates both
// against one .expected.  That worked only as long as the answers agreed, and they agreed
// only because /etc/ttys did not exist.  Kernel task 29b put it on the image, so:
//
//   under b6sim   ttyname() reads /dev with read(2) and the host refuses to read a directory,
//                 so every answer is NULL and ttyslot() is 0.
//   on the image  /dev really is a directory this kernel will read, and /etc/ttys really is
//                 there, so ttyname(0) is /dev/console and ttyslot() is 1.
//
// One expectation cannot say both, so this program is IMAGEONLY -- the same reason memt,
// shellt, suidt and curstty are (progs.cmake).
//
// WHY THE ANSWERS ARE WHAT THEY ARE, since each is a claim about a different file:
//
//   ttyname(0) == "/dev/console".  The deleted image-side runner redirected only 1 and 2,
//   so 0 is still the shell's terminal.  ttyname() fstat()s it and scans /dev for a character
//   special file with the same I-NUMBER -- not the same device number, which would be
//   ambiguous here: root.manifest gives /dev/console and /dev/tty0 the same major and minor
//   deliberately, under two separate inodes, so that each name reports itself.
//
//   ttyslot() == 1.  It takes the last component of that name, `console', and counts lines of
//   /etc/ttys until one matches, past the two flag characters each line begins with.
//   `console' is the first of the two lines, and the count is one-based.
//
//   getlogin() == NULL, and that is not a failure.  It reads slot ttyslot() of /etc/utmp, and
//   /etc/utmp does not exist yet when this runs: cmd/init/init.c creates it in merge(), which
//   is a stage the boot does not reach until the single-user shell has exited -- and every
//   libtest program runs at that first prompt.  What a logged-in getlogin() answers is
//   kernel/test/login's business, not this program's.
//
#include <stdio.h>
#include <string.h>
#include <unistd.h> // ttyname, ttyslot, getlogin -- all three declared there since task C6

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

int main(void)
{
    char *tp;

    printf("--- ttyname\n");
    tp = ttyname(0);
    printf("ttyname(0) %s\n", tp ? tp : "(null)");
    ok("standard input is a terminal", tp != 0);
    ok("and it is the console", tp != 0 && strcmp(tp, "/dev/console") == 0);
    ok("a descriptor that is not open has no name", ttyname(31) == 0);

    printf("--- ttyslot\n");
    printf("ttyslot() %d\n", ttyslot());
    ok("the console is line 1 of /etc/ttys", ttyslot() == 1);

    printf("--- getlogin\n");
    ok("nobody is logged in on it yet", getlogin() == 0);

    printf("done\n");
    return 0;
}
