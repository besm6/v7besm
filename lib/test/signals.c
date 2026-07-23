/*
 * signals -- signal delivery, end to end: the frame the system builds, the handler it
 * enters, and the return through it.
 *
 * What is under test is mostly NOT libc, which contributes only raise() and sleep()
 * here.  It is the frame: kernel/sendsig.c pushes the 21-word reg.h frame on the user
 * stack, plants a `$77 SYS_sigret' word above it as the handler's return address and
 * enters the handler with the signal number in the accumulator; the handler returns
 * into that word and sigret() reloads the frame.  b6sim does the same, at the same
 * point -- the end of a serviced extracode -- so this program is the same program on
 * both (cmd/sim/syscall.cpp).
 *
 * The checks that would catch a frame built wrongly:
 *
 *   * THE NUMBER ARRIVES.  A handler is `void (*)(int)' (C11 §7.14.1.1) and its one
 *     argument travels in the accumulator, so a frame that forgot to set it enters the
 *     handler with whatever the interrupted code had there.
 *   * THE INTERRUPTED CONTEXT COMES BACK.  main() holds live values across a delivery
 *     -- an auto, a walked pointer and a sum built one term at a time -- and the
 *     handler deliberately clobbers registers by running a call chain of its own.
 *   * RETURN VALUE AND errno SURVIVE.  pause() returns -1/EINTR, and both are read
 *     AFTER the handler has run: the accumulator and r14 are two of the words the
 *     frame restores, and they are also the two the syscall return path would have
 *     overwritten had sigret() not said not to (u.u_justreturn).
 *   * A HANDLER MAY NEST.  hangup() raises SIGTERM from inside itself, so a second
 *     frame is built on top of the first and unwound in order.
 *   * A HANDLER MAY LEAVE BY longjmp.  sleep() does exactly that, abandoning the frame
 *     on the user stack -- which costs nothing, and is the reason the frame lives there
 *     rather than in the u-area.
 *
 * NOTHING IS PRINTED FROM A HANDLER.  Delivery lands at the end of a syscall, and one
 * of the syscalls a printf makes is the write() it ends with, so a handler that printed
 * could re-enter stdio on its own buffer.  Handlers record; main() prints.
 *
 * sleep(1) makes this the one test that takes a second of wall clock.  It is worth it:
 * it is the only path here that never returns through the frame at all.
 */
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

/* No header declares these four: <signal.h> names signal and raise and no more. */
int alarm(int sec);
int pause(void);
int getpid(void);
void sleep(unsigned n);

static int caught[NSIG]; /* how many times each signal was handled */
static int number;       /* the argument the last handler was given */
static int depth;        /* handlers currently running, and the deepest nesting */
static int deepest;

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

/*
 * A call chain the handler runs before returning, so that the registers a handler is
 * free to clobber -- r8..r14, the accumulator, R -- really are clobbered.  Anything the
 * frame fails to restore shows up as a wrong answer in main() rather than as a crash.
 */
static int churn(int n)
{
    if (n <= 0)
        return 1;
    return n * churn(n - 1) + 1;
}

static void catcher(int sig)
{
    depth++;
    if (depth > deepest)
        deepest = depth;
    number = sig;
    if (sig > 0 && sig < NSIG)
        caught[sig]++;
    churn(6);
    depth--;
}

/* Raises another signal from inside a handler: a frame on top of a frame. */
static void hangup(int sig)
{
    depth++;
    if (depth > deepest)
        deepest = depth;
    caught[sig]++;
    signal(SIGTERM, catcher);
    raise(SIGTERM);
    depth--;
}

static jmp_buf away;

static void leaper(int sig)
{
    caught[sig]++;
    longjmp(away, 1);
}

int main()
{
    void (*old)(int);
    int i, sum, r;
    char *p, buf[8];

    /* ---- the disposition, and what signal() answers with ---------------------- */

    old = signal(SIGTERM, catcher);
    ok("signal returns the old disposition (SIG_DFL)", old == SIG_DFL);
    old = signal(SIGTERM, catcher);
    ok("signal returns the handler just installed", old == catcher);
    ok("signal rejects SIGKILL", signal(SIGKILL, catcher) == SIG_ERR);
    ok("...with EINVAL", errno == EINVAL);

    /* ---- a caught signal, and the number it carries --------------------------- */

    number = -1;
    ok("raise succeeds", raise(SIGTERM) == 0);
    ok("the handler ran", caught[SIGTERM] == 1);
    ok("the handler was told which signal", number == SIGTERM);

    /* v7 resets a caught signal to SIG_DFL as it delivers it (psig(), sig.c), so
     * this reads back the reset and not the handler. */
    old = signal(SIGTERM, catcher);
    ok("a caught signal was reset to SIG_DFL", old == SIG_DFL);

    /* ---- the interrupted context ---------------------------------------------- */

    sum = 0;
    p   = buf;
    for (i = 0; i < 6; i++) {
        *p++ = (char)('a' + i);
        sum += i * 10;
        if (i == 3)
            raise(SIGTERM); /* mid-loop, with everything live */
    }
    *p = '\0';
    ok("an auto survived the delivery", i == 6);
    ok("a sum built across it is right", sum == 150);
    ok("a walked pointer survived", p == buf + 6);
    ok("and what it wrote is intact", buf[0] == 'a' && buf[5] == 'f');
    ok("the handler ran once more", caught[SIGTERM] == 2);

    /* ---- SIG_IGN -------------------------------------------------------------- */

    old = signal(SIGINT, SIG_IGN);
    ok("SIG_IGN installs", old == SIG_DFL);
    raise(SIGINT);
    ok("an ignored signal runs no handler", caught[SIGINT] == 0);
    ok("...and stays ignored", signal(SIGINT, SIG_DFL) == SIG_IGN);

    /* ---- nesting -------------------------------------------------------------- */

    deepest = 0;
    signal(SIGHUP, hangup);
    raise(SIGHUP);
    ok("the outer handler ran", caught[SIGHUP] == 1);
    ok("the inner handler ran", caught[SIGTERM] == 3);
    ok("one frame on top of the other", deepest == 2);

    /* ---- alarm and pause, and the result the frame carries back ---------------- */

    signal(SIGALRM, catcher);
    number = -1;
    alarm(1);
    errno = 0;
    r     = pause();
    ok("pause returns -1 through the frame", r == -1);
    ok("...with EINTR, likewise", errno == EINTR);
    ok("the alarm was delivered", caught[SIGALRM] == 1);
    ok("with its own number", number == SIGALRM);

    /* ---- a handler that never returns ------------------------------------------ */

    signal(SIGALRM, leaper);
    if (setjmp(away) == 0) {
        alarm(1);
        for (;;)
            pause();
    }
    ok("a handler may longjmp out of the frame", caught[SIGALRM] == 2);
    alarm(0);

    /* sleep() is that same trick, with the alarm dance libc does around it. */
    sleep(1);
    ok("sleep returns", 1);

    printf("done\n");
    return 0;
}
