// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include "sys/dir.h"
#include "sys/inode.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/signal.h"
#include "sys/systm.h"
#include "sys/text.h"
#include "sys/types.h"
#include "sys/user.h"

// Priority for tracing
#define IPCPRI PZERO

// Tracing variables.
// Used to pass trace command from
// parent to child being traced.
// This data base cannot be
// shared and is locked
// per user.
//
// ip_addr IS AN `int *', AND THAT IS THE ABI.  It arrives as the caller's third argument
// untouched (ptrace() below), so what ptrace(2) promises is a WORD address -- a thin
// pointer, bits 15-1 -- and not a char *.  Two things follow.  `(caddr_t)ip_addr' in
// procxmt() is a real conversion and yields byte #0 of that word, which is why those
// casts need no `(int *)' step; and a caller that hands over a mid-word char * has its
// byte offset silently dropped by fuword()/suword(), flooring to the containing word.
// The units differ per request -- an address in the child for 1/2/4/5, a word INDEX into
// the u-area for 3/6, a resume PC for 7/9 -- and doc/Unix_V7_System_Calls.md carries the
// table.
struct {
    int ip_lock;
    int ip_req;
    int *ip_addr;
    int ip_data;
} ipc;

// Send the specified signal to
// all processes with 'pgrp' as
// process group.
// Called by tty.c for quits and
// interrupts.
void signal(register int pgrp, int sig)
{
    register struct proc *p;

    if (pgrp == 0)
        return;
    for (p = &proc[0]; p < &proc[NPROC]; p++)
        if (p->p_pgrp == pgrp)
            psignal(p, sig);
}

// Send the specified signal to
// the specified process.
void psignal(register struct proc *p, register int sig)
{
    // v7 wrote this as `(unsigned)sig >= NSIG' to fold the negative test in;
    // here that costs a b$uge call, and the two signed tests are inline.
    if (sig < 0 || sig >= NSIG)
        return;
    if (sig)
        p->p_sig |= 1 << (sig - 1);
    if (p->p_pri > PUSER)
        p->p_pri = PUSER;
    if (p->p_stat == SSLEEP && p->p_pri > PZERO)
        setrun(p);
}

// Returns true if the current
// process has a signal to process.
// This is asked at least once
// each time a process enters the
// system.
// A signal does not do anything
// directly to a process; it sets
// a flag that asks the process to
// do something to itself.
int issig()
{
    register int n;
    register struct proc *p;

    p = u.u_procp;
    while (p->p_sig) {
        n = fsig(p);
        if (u.u_signal[n] != 1 || (p->p_flag & STRC))
            return (n);
        p->p_sig &= ~(1 << (n - 1));
    }
    return (0);
}

// Enter the tracing STOP state.
// In this state, the parent is
// informed and the process is able to
// receive commands from the parent.
void stop()
{
    register struct proc *pp, *cp;

loop:
    cp = u.u_procp;
    if (cp->p_ppid != 1)
        for (pp = &proc[0]; pp < &proc[NPROC]; pp++)
            if (pp->p_pid == cp->p_ppid) {
                wakeup((chan_t)pp);
                cp->p_stat = SSTOP;
                swtch();
                if ((cp->p_flag & STRC) == 0 || procxmt())
                    return;
                goto loop;
            }
    exit(fsig(u.u_procp));
}

// Perform the action specified by
// the current signal.
// The usual sequence is:
// 	if(issig())
// 		psig();
void psig()
{
    register int n, p;
    register struct proc *rp;

    rp = u.u_procp;
    if (rp->p_flag & STRC)
        stop();
    n = fsig(rp);
    if (n == 0)
        return;
    rp->p_sig &= ~(1 << (n - 1));
    if ((p = u.u_signal[n]) != 0) {
        u.u_error = 0;
        if (n != SIGILL && n != SIGTRAP)
            u.u_signal[n] = 0;
        sendsig(p, n);
        return;
    }
    switch (n) {
    case SIGQUIT:
    case SIGILL:
    case SIGTRAP:
    case SIGABRT:
    case SIGEMT:
    case SIGFPE:
    case SIGBUS:
    case SIGSEGV:
    case SIGSYS:
        if (core())
            n += 0200;
    }
    exit(n);
}

// find the signal in bit-position
// representation in p_sig.
int fsig(struct proc *p)
{
    register int n, i;

    n = p->p_sig;
    for (i = 1; i < NSIG; i++) {
        if (n & 1)
            return (i);
        n >>= 1;
    }
    return (0);
}

// Create a core image on the file "core"
// If you are looking for protection glitches,
// there are probably a wealth of them here
// when this occurs to a suid command.
//
// ONE OF THEM IS CLOSED: the gate below tests the GROUP as well as the user, which is
// 4.2BSD's condition and not v7's.  access() compares the EFFECTIVE ids (kernel/fio.c), so
// a set-group-id process could otherwise have dropped a core image -- the whole u-area,
// and whatever it had read -- into a directory its borrowed group could write and its
// caller could not.  Nothing on this image carries ISGID today (root.manifest has only
// 04755), so this shuts a door before anybody walks through it.
//
// It writes USIZE block of the
// user.h area followed by the entire
// data+stack segments.
int core()
{
    register struct inode *ip;
    register int s;

    u.u_error = 0;
    u.u_dirp  = "core";
    ip        = namei(schar, 1);
    if (ip == NULL) {
        if (u.u_error)
            return (0);
        ip = maknode(0666);
        if (ip == NULL)
            return (0);
    }
    if (!access(ip, IWRITE) && (ip->i_mode & IFMT) == IFREG && u.u_uid == u.u_ruid &&
        u.u_gid == u.u_rgid) {
        itrunc(ip);
        u.u_offset = 0;
        // The SAVED u-area is a single page: struct user at the bottom, the
        // kernel stack above it.  Two blocks (USIZE / BSIZEW).  The overflow
        // page above it is not part of the image and is not dumped -- a core
        // file has whatever frames were in the saved half.
        //
        // The whole page, not u_stkdepth words: the size is part of the core
        // file's layout and every offset past it would move.  Since task 30
        // that means the tail above the live depth holds whatever the
        // PREVIOUSLY resumed process left on the stack, where before it held
        // this process's own dead frames.  Accepted, and noted in
        // doc/Besm6_Kernel_Reference.md's consequences list; ptrace's u-area window
        // (procxmt() below) reads the same words and says the same thing.
        u.u_base   = (caddr_t)&u;
        u.u_count  = wtob(USIZE);
        u.u_segflg = 1;
        writei(ip);
        s = u.u_procp->p_size - USIZE;
        estabur(0, s, 0);
        // `(caddr_t)(int *)0', never `(caddr_t)0'.  The bare form is a bit COPY -- the
        // compiler emits no marker and leaves the byte field 0, which reads as byte #5, a
        // word's LAST -- so this base stood out of phase with the kernel buffer and
        // copyinb() funnelled the FIRST 3072-byte chunk five bytes over.  Only the first:
        // iomove() then walks the base with `u.u_base += n', and the walked value is
        // well-formed, so the thirteen chunks after it were in phase and right.  The
        // damage was therefore silent and bounded -- virtual words 0..511 of every core
        // image, the header hole and the start of const+text -- and the file was always
        // its full p_size words.  Measured with ucopy.c's nioshift, which rose by exactly
        // one block.  Same idiom and same fix as getxfile()'s data read; sys1.c says it
        // at length.  kernel/test/core is what holds this line down.
        u.u_base   = (caddr_t)(int *)0;
        u.u_count  = wtob(s);
        u.u_segflg = 0;
        writei(ip);
    }
    iput(ip);
    return (u.u_error == 0);
}

// Grow the stack to include the faulting virtual PAGE.  A page number is all the machine
// reports -- ГРП bits 5-9, see trap.c -- so that is what this takes.  True return if it grew.
//
// The stack occupies virtual pages USTKPAGE .. USTKPAGE + u_ssize/PGSZ - 1, growing UP, and
// its physical pages are the tail of the image (sureg(), utab.c).  A new page is therefore
// appended at BOTH ends at once -- the next higher virtual page and the end of the image --
// so every existing stack page keeps the address it had.  With an upward stack there is
// nothing to move, so growing the stack needs no copyseg shuffle at all.
//
// The ceiling needs no guard of its own: estabur() rejects ns > (NPAGE - USTKPAGE) * PGSZ.
int grow(int pg)
{
    register int si, i;
    register struct proc *p;
    register int a;

    if (pg < USTKPAGE || pg >= NPAGE)
        return (0); // not a stack page at all
    si = (pg - USTKPAGE + 1) * PGSZ - u.u_ssize;
    if (si <= 0)
        return (0); // already mapped: this was not a stack fault
    if (si < SINCR)
        si = SINCR;
    // estabur() assigns u_ssize itself, so there is no trailing `u.u_ssize += si' here
    // -- that would count the growth twice.
    if (estabur(u.u_tsize, u.u_dsize, u.u_ssize + si))
        return (0);
    p = u.u_procp;
    expand(p->p_size + si);
    // The new pages are the tail of the grown image; expand() may have relocated it.
    a = p->p_addr + p->p_size - si;
    for (i = si; i > 0; i -= PGSZ) {
        clearseg(a);
        a += PGSZ;
    }
    return (1);
}

// sys-trace system call.
void ptrace()
{
    register struct proc *p;
    register struct a {
        int req;
        int pid;
        int *addr;
        int data;
    } *uap;

    uap = (struct a *)u.u_ap;
    if (uap->req <= 0) {
        u.u_procp->p_flag |= STRC;
        return;
    }
    for (p = proc; p < &proc[NPROC]; p++)
        if (p->p_stat == SSTOP && p->p_pid == uap->pid && p->p_ppid == u.u_procp->p_pid)
            goto found;
    u.u_error = ESRCH;
    return;

found:
    while (ipc.ip_lock)
        sleep((chan_t)&ipc, IPCPRI);
    ipc.ip_lock = p->p_pid;
    ipc.ip_data = uap->data;
    ipc.ip_addr = uap->addr;
    ipc.ip_req  = uap->req;
    p->p_flag &= ~SWTED;
    setrun(p);
    while (ipc.ip_req > 0)
        sleep((chan_t)&ipc, IPCPRI);
    u.u_r.r_val1 = ipc.ip_data;
    if (ipc.ip_req < 0)
        u.u_error = EIO;
    ipc.ip_lock = 0;
    wakeup((chan_t)&ipc);
}

// Code that the child process
// executes to implement the command
// of the parent process in tracing.
int procxmt()
{
    register int i;
    register int *p;
    register struct text *xp;

    if (ipc.ip_lock != u.u_procp->p_pid)
        return (0);
    i          = ipc.ip_req;
    ipc.ip_req = 0;
    wakeup((chan_t)&ipc);
    switch (i) {
    // read user I
    // read user D
    case 1:
    case 2:
        if (fubyte((caddr_t)ipc.ip_addr) == -1)
            goto error;
        ipc.ip_data = fuword((caddr_t)ipc.ip_addr);
        break;

    // read u
    case 3:
        // the u-area is one page; ip_addr is a word index into it
        i = (int)ipc.ip_addr;
        if (i < 0 || i >= USIZE)
            goto error;
        ipc.ip_data = ((int *)&u)[i];
        break;

    // write user I
    // Must set up to allow writing
    case 4:
        // If text, must assure exclusive use
        if ((xp = u.u_procp->p_textp)) {
            if (xp->x_count != 1 || xp->x_iptr->i_mode & ISVTX)
                goto error;
            xp->x_iptr->i_flag &= ~ITEXT;
        }
        // v7's estabur(RW)/estabur(RO) bracket: the same call twice here, no page
        // being read-only, and kept only for the sureg() it re-runs.
        estabur(u.u_tsize, u.u_dsize, u.u_ssize);
        i = suword((caddr_t)ipc.ip_addr, 0);
        suword((caddr_t)ipc.ip_addr, ipc.ip_data);
        estabur(u.u_tsize, u.u_dsize, u.u_ssize);
        if (i < 0)
            goto error;
        if (xp)
            xp->x_flag |= XWRIT;
        break;

    // write user D
    case 5:
        if (suword((caddr_t)ipc.ip_addr, 0) < 0)
            goto error;
        suword((caddr_t)ipc.ip_addr, ipc.ip_data);
        break;

    // write u
    case 6:
        // ip_addr is a word index into the u-area, as in case 3 above -- and bounded here
        // as it is there.  The regloc[] walk below is what gates the store, so an index off
        // the end was never written through; it was still formed into a pointer first.
        i = (int)ipc.ip_addr;
        if (i < 0 || i >= USIZE)
            goto error;
        p = &((int *)&u)[i];
        for (i = 0; i < 16; i++)
            if (p == &u.u_ar0[regloc[i]])
                goto ok;
        // There is no flags register in the frame to write: the machine's trace
        // hardware is М034/М035, which is rewritten rather than remapped.
        goto error;

    ok:
        *p = ipc.ip_data;
        break;

    // continue and stop again after one instruction: not offered here.  There is no
    // T-bit, and stepping with the address-break registers М034/М035 would need an
    // instruction decoder in the kernel -- doc/Besm6_Kernel_Reference.md, "Known
    // consequences, accepted".
    // The child stays stopped, so the parent can retry with request 7.
    case 9:
        goto error;

    // set signal and continue
    case 7:
        if ((int)ipc.ip_addr != 1)
            // The single RET slot holds whichever return the stopping gate saved
            // (ERET for a syscall stop, IRET for a fault/signal stop).
            u.u_ar0[RET] = (int)ipc.ip_addr;
        u.u_procp->p_sig = 0;
        if (ipc.ip_data)
            psignal(u.u_procp, ipc.ip_data);
        return (1);

    // force exit
    case 8:
        exit(fsig(u.u_procp));

    default:
    error:
        ipc.ip_req = -1;
    }
    return (0);
}
