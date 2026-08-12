/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// update -- flush the buffer cache to disk every thirty seconds.  Task C20; README.md is
// the account, and it says why one /etc/rc line cannot leave two of these running.
//
// The fork is load-bearing: runcom() (../init/init.c) waits for the shell running /etc/rc.
// v7's dosync()/alarm(30)/pause() dance is sleep(3) (../../lib/libc/gen/sleep.c), so the
// loop is written out here; its three opens of /bin, /usr and /usr/bin are dropped.
//
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    if (fork())
        exit(0); // the parent goes, so /etc/rc does not wait for what never returns

    // init opened `/' three times to give the script descriptors 0, 1 and 2.
    close(0);
    close(1);
    close(2);

    for (;;) {
        sync();
        sleep(30);
    }
    // NOTREACHED
}
