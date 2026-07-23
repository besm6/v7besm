// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Suspend for n seconds, preserving whatever alarm was already pending.
//
// This is the routine that needs a handler of its own to be DELIVERED, and the one that
// leaves through a longjmp rather than returning: sleepx() below never comes back to the
// signal frame the kernel built, and the process resumes at the setjmp with the stack
// unwound past it.  That works because the frame lives on the user stack -- abandoning
// it costs nothing (kernel/sendsig.c).
//
// The dance around the caller's alarm is v7's and is worth restating: an alarm(1000) is
// planted first so the old setting can be read out of alarm()'s return value without a
// window in which no alarm is pending at all, and if the caller's alarm was due sooner
// than n seconds, it wins and this call returns early.
//
#include <setjmp.h>
#include <signal.h>

int alarm(int sec);
int pause(void);

static jmp_buf jmp;

//
// The handler takes the signal number and returns void, per C11 SS7.14.1.1 and
// <signal.h>.  It used to declare signal() itself right here, because v7's
// header offered only `int (*signal())();' -- no prototype -- and the number is
// ignored: there is one signal this can be entered for.
//
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
    altime = alarm(1000); // time to maneuver
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
    // NOTREACHED
}
