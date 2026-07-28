/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tty -- print the path name of the standard input's terminal.
//
//      tty        print it, or `not a tty'
//      tty -s     print nothing; report through the exit status alone
//
// One of task C2b's five (../README.md).  Everything it does is ttyname(3), and there is
// nothing machine-dependent left for this file to get wrong: no pointer comparison (§2), no
// long, no buffer.  The C11 pass is the whole of the diff, and one line of it matters -- the
// K&R `char *ttyname();' at the top had to GO rather than be modernized, since
// <unistd.h> declares the function and a second declaration of a different shape is an
// error, not a redundancy.  v7 also called printf and strcmp with no #include at all.
//
// WHICH WORLD SAYS WHAT, and it is the reason this program is worth testing twice
// (../README.md §9).  lib/libc/gen/ttyname.c answers by fstat()ing the descriptor and then
// scanning /dev with an ordinary read(2) on the directory, looking for a character special
// file with the same device number.  Under the booted kernel that works and the answer is
// /dev/console -- uniquely, because root.manifest gives the console major 0 and /dev/tty
// major 2, so exactly one entry matches.  Under b6sim it CANNOT work: read(2) there is the
// host's, and the host refuses to read a directory, so ttyname() answers NULL whatever the
// descriptor is.  Both are deterministic, so both are asserted -- test/ says `not a tty'
// and kernel/test/utils.sh says /dev/console.
//
// NOT SETUID: fstat and read on /dev need no privilege.
//
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    char *p;

    p = ttyname(0);
    if (argc != 2 || strcmp(argv[1], "-s") != 0)
        printf("%s\n", p ? p : "not a tty");
    return p ? 0 : 1;
}
