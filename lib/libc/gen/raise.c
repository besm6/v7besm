// C11 §7.14.2.1.  v7 had no such call; written here.

//
// raise(sig) -- send the signal to the calling process, and do not come back until
// it has been handled (§7.14.2.1).
//
// There is nothing to arrange for that last part on this system: kill() to one's own
// process raises the signal in the kernel's p_sig, and the syscall's own return path
// delivers it -- psig() runs in sysret() (kernel/syscall.c) before the gate restores
// the frame, so the handler has already run and returned through the signal frame by
// the time this call yields a value.  b6sim delivers at the same point.
//
// The return value is kill()'s, which is 0/-1 rather than §7.14.2.1's "nonzero if
// unsuccessful" -- the same thing said two ways.
//
#include <signal.h>

int getpid(void);
int kill(int pid, int sig);

int raise(int sig)
{
    return kill(getpid(), sig);
}
