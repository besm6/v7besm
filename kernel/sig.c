/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/inode.h"
#include "sys/reg.h"
#include "sys/text.h"
#include "sys/seg.h"
// clang-format on

/*
 * Priority for tracing
 */
#define IPCPRI PZERO

/*
 * Tracing variables.
 * Used to pass trace command from
 * parent to child being traced.
 * This data base cannot be
 * shared and is locked
 * per user.
 */
struct {
    int ip_lock;
    int ip_req;
    int *ip_addr;
    int ip_data;
} ipc;

/*
 * Send the specified signal to
 * all processes with 'pgrp' as
 * process group.
 * Called by tty.c for quits and
 * interrupts.
 */
void signal(register int pgrp, int sig)
{
    register struct proc *p;

    if (pgrp == 0)
        return;
    for (p = &proc[0]; p < &proc[NPROC]; p++)
        if (p->p_pgrp == pgrp)
            psignal(p, sig);
}

/*
 * Send the specified signal to
 * the specified process.
 */
void psignal(register struct proc *p, register int sig)
{
    if ((unsigned)sig >= NSIG)
        return;
    if (sig)
        p->p_sig |= 1 << (sig - 1);
    if (p->p_pri > PUSER)
        p->p_pri = PUSER;
    if (p->p_stat == SSLEEP && p->p_pri > PZERO)
        setrun(p);
}

/*
 * Returns true if the current
 * process has a signal to process.
 * This is asked at least once
 * each time a process enters the
 * system.
 * A signal does not do anything
 * directly to a process; it sets
 * a flag that asks the process to
 * do something to itself.
 */
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

/*
 * Enter the tracing STOP state.
 * In this state, the parent is
 * informed and the process is able to
 * receive commands from the parent.
 */
void stop()
{
    register struct proc *pp, *cp;

loop:
    cp = u.u_procp;
    if (cp->p_ppid != 1)
        for (pp = &proc[0]; pp < &proc[NPROC]; pp++)
            if (pp->p_pid == cp->p_ppid) {
                wakeup((caddr_t)pp);
                cp->p_stat = SSTOP;
                swtch();
                if ((cp->p_flag & STRC) == 0 || procxmt())
                    return;
                goto loop;
            }
    exit(fsig(u.u_procp));
}

/*
 * Perform the action specified by
 * the current signal.
 * The usual sequence is:
 *	if(issig())
 *		psig();
 */
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
        if (n != SIGINS && n != SIGTRC)
            u.u_signal[n] = 0;
        sendsig((caddr_t)p, n);
        return;
    }
    switch (n) {
    case SIGQUIT:
    case SIGINS:
    case SIGTRC:
    case SIGIOT:
    case SIGEMT:
    case SIGFPT:
    case SIGBUS:
    case SIGSEG:
    case SIGSYS:
        if (core())
            n += 0200;
    }
    exit(n);
}

/*
 * find the signal in bit-position
 * representation in p_sig.
 */
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

/*
 * Create a core image on the file "core"
 * If you are looking for protection glitches,
 * there are probably a wealth of them here
 * when this occurs to a suid command.
 *
 * It writes USIZE block of the
 * user.h area followed by the entire
 * data+stack segments.
 */
int core()
{
    register struct inode *ip;
    register unsigned s;

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
    if (!access(ip, IWRITE) && (ip->i_mode & IFMT) == IFREG && u.u_uid == u.u_ruid) {
        itrunc(ip);
        u.u_offset = 0;
        /*
         * The u-area is a single page: struct user at the bottom, the kernel
         * stack above it.  One block.
         */
        u.u_base   = (caddr_t)&u;
        u.u_count  = wtob(USIZE);
        u.u_segflg = 1;
        writei(ip);
        s = u.u_procp->p_size - USIZE;
        estabur((unsigned)0, s, (unsigned)0, 0, RO);
        u.u_base   = 0;
        u.u_count  = wtob(s);
        u.u_segflg = 0;
        writei(ip);
    }
    iput(ip);
    return (u.u_error == 0);
}

/*
 * Grow the stack to include the faulting virtual PAGE.  A page number is all the machine
 * reports -- ГРП bits 5-9, see trap.c -- so that is what this takes.  True return if it grew.
 *
 * The stack occupies virtual pages USTKPAGE .. USTKPAGE + u_ssize/PGSZ - 1, growing UP, and
 * its physical pages are the tail of the image (sureg(), utab.c).  A new page is therefore
 * appended at BOTH ends at once -- the next higher virtual page and the end of the image --
 * so every existing stack page keeps the address it had.  With an upward stack there is
 * nothing to move, so growing the stack needs no copyseg shuffle at all.
 *
 * The ceiling needs no guard of its own: estabur() rejects ns > (NPAGE - USTKPAGE) * PGSZ.
 */
int grow(unsigned pg)
{
    register int si, i;
    register struct proc *p;
    register unsigned a;

    if (pg < USTKPAGE || pg >= NPAGE)
        return (0); /* not a stack page at all */
    si = (pg - USTKPAGE + 1) * PGSZ - u.u_ssize;
    if (si <= 0)
        return (0); /* already mapped: this was not a stack fault */
    if (si < SINCR)
        si = SINCR;
    /*
     * estabur() assigns u_ssize itself, so there is no trailing `u.u_ssize += si' here
     * -- that would count the growth twice.
     */
    if (estabur(u.u_tsize, u.u_dsize, u.u_ssize + si, u.u_sep, RO))
        return (0);
    p = u.u_procp;
    expand(p->p_size + si);
    /* The new pages are the tail of the grown image; expand() may have relocated it. */
    a = p->p_addr + p->p_size - si;
    for (i = si; i > 0; i -= PGSZ) {
        clearseg(a);
        a += PGSZ;
    }
    return (1);
}

/*
 * sys-trace system call.
 */
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
        sleep((caddr_t)&ipc, IPCPRI);
    ipc.ip_lock = p->p_pid;
    ipc.ip_data = uap->data;
    ipc.ip_addr = uap->addr;
    ipc.ip_req  = uap->req;
    p->p_flag &= ~SWTED;
    setrun(p);
    while (ipc.ip_req > 0)
        sleep((caddr_t)&ipc, IPCPRI);
    u.u_r.r_val1 = ipc.ip_data;
    if (ipc.ip_req < 0)
        u.u_error = EIO;
    ipc.ip_lock = 0;
    wakeup((caddr_t)&ipc);
}

/*
 * Code that the child process
 * executes to implement the command
 * of the parent process in tracing.
 */
int procxmt()
{
    register int i;
    register int *p;
    register struct text *xp;

    if (ipc.ip_lock != u.u_procp->p_pid)
        return (0);
    i          = ipc.ip_req;
    ipc.ip_req = 0;
    wakeup((caddr_t)&ipc);
    switch (i) {
    /* read user I */
    /* read user D */
    case 1:
    case 2:
        if (fubyte((caddr_t)ipc.ip_addr) == -1)
            goto error;
        ipc.ip_data = fuword((caddr_t)ipc.ip_addr);
        break;

    /* read u */
    case 3:
        /* the u-area is one page; ip_addr is a word index into it */
        i = (int)ipc.ip_addr;
        if (i < 0 || i >= USIZE)
            goto error;
        ipc.ip_data = ((physadr)&u)->r[i];
        break;

    /* write user I */
    /* Must set up to allow writing */
    case 4:
        /*
         * If text, must assure exclusive use
         */
        if ((xp = u.u_procp->p_textp)) {
            if (xp->x_count != 1 || xp->x_iptr->i_mode & ISVTX)
                goto error;
            xp->x_iptr->i_flag &= ~ITEXT;
        }
        estabur(u.u_tsize, u.u_dsize, u.u_ssize, u.u_sep, RW);
        i = suword((caddr_t)ipc.ip_addr, 0);
        suword((caddr_t)ipc.ip_addr, ipc.ip_data);
        estabur(u.u_tsize, u.u_dsize, u.u_ssize, u.u_sep, RO);
        if (i < 0)
            goto error;
        if (xp)
            xp->x_flag |= XWRIT;
        break;

    /* write user D */
    case 5:
        if (suword((caddr_t)ipc.ip_addr, 0) < 0)
            goto error;
        suword((caddr_t)ipc.ip_addr, ipc.ip_data);
        break;

    /* write u */
    case 6:
        /* ip_addr is a word index into the u-area, as in case 3 above */
        i = (int)ipc.ip_addr;
        p = (int *)&((physadr)&u)->r[i];
        for (i = 0; i < 16; i++)
            if (p == &u.u_ar0[(unsigned)regloc[i]])
                goto ok;
        /* TODO 17: there is no flags register in the frame; single-step /
         * address-break is М034/М035 (rewritten, not remapped). */
        goto error;

    ok:
        *p = ipc.ip_data;
        break;

    /* set signal and continue */
    /*  one version causes a trace-trap */
    case 9:
        /* TODO 17: arm single-step via the address-break registers М034/М035
         * (not a flag bit).  Falls through to case 7 to set the resume PC. */
    case 7:
        if ((int)ipc.ip_addr != 1)
            /* The single RET slot holds whichever return the stopping gate saved
             * (ERET for a syscall stop, IRET for a fault/signal stop). */
            u.u_ar0[RET] = (int)ipc.ip_addr;
        u.u_procp->p_sig = 0;
        if (ipc.ip_data)
            psignal(u.u_procp, ipc.ip_data);
        return (1);

    /* force exit */
    case 8:
        exit(fsig(u.u_procp));

    default:
    error:
        ipc.ip_req = -1;
    }
    return (0);
}
