/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Suspend for n seconds, preserving whatever alarm was already pending.
 *
 * NOT RUNNABLE YET, and it links only.  It needs a handler of its own to be DELIVERED
 * -- sleepx() below -- and signal delivery is phase 6 of lib/README.md, blocked on the
 * kernel: sendsig() in kernel/machdep.c pushes one word and jumps, with no signal frame
 * designed yet.  b6sim is no better: its SYS_signal accepts SIG_DFL and SIG_IGN and
 * answers anything else with EINVAL, because a guest handler cannot be run from a host
 * signal context (cmd/sim/syscall.cpp).  So this is ported now, with the rest of gen,
 * and left untested until the frame exists.
 *
 * The dance around the caller's alarm is v7's and is worth restating: an alarm(1000) is
 * planted first so the old setting can be read out of alarm()'s return value without a
 * window in which no alarm is pending at all, and if the caller's alarm was due sooner
 * than n seconds, it wins and this call returns early.
 */
#include <setjmp.h>
#include <signal.h>

int alarm(int sec);
int pause(void);

static jmp_buf jmp;

/*
 * The handler takes the signal number and returns void, per C11 SS7.14.1.1 and
 * <signal.h>.  It used to declare signal() itself right here, because v7's
 * header offered only `int (*signal())();' -- no prototype -- and the number is
 * ignored: there is one signal this can be entered for.
 */
static void sleepx(int sig)
{
    longjmp(jmp, 1);
}

void sleep(unsigned n)
{
    unsigned altime;
    void (*alsig)(int) = SIG_DFL;

    if (n == 0)
        return;
    altime = alarm(1000); /* time to maneuver */
    if (setjmp(jmp)) {
        signal(SIGALRM, alsig);
        alarm(altime);
        return;
    }
    if (altime) {
        if (altime > n)
            altime -= n;
        else {
            n      = altime;
            altime = 1;
        }
    }
    alsig = signal(SIGALRM, sleepx);
    alarm(n);
    for (;;)
        pause();
    /*NOTREACHED*/
}
