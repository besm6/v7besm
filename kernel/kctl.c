// kctl(2): the kernel-variable table, and the per-process digest beside it.
// <sys/kctl.h> is the interface and says why the call exists.
//
// THE TABLE IS NOT THE KERNEL'S SYMBOL TABLE.  It is the set of variables the inspection
// programs ask for, and every row names the program that asks; a row nobody reads is a
// promise kept for nothing.  Each address is a link-time relocation of the REAL declaration,
// pulled in through the headers below, so renaming or deleting one of these variables stops
// this file compiling rather than leaving the table pointing at rubble -- which is what a
// /unix on the disk and nlist(3) could never have promised.
//
// ks_addr IS A void * AND THAT IS FORCED, NOT CHOSEN.  This compiler folds an address
// constant from &lvalue, an array or function name, and address +- constant, and from
// nothing else -- there is no case for a CAST -- so `(int *)proc' does not compile and a
// bare object name only type-checks when the pointee is void.  It is stored as a FAT pointer,
// hence ptrword() on every read; doc/Besm6_Data_Representation.md section 7 is the account.
//
// A SIZE IS NEVER A HAND-WRITTEN NUMBER: sizeof where the extern carries its bound, and
// otherwise the same count macro kernel/conf.c dimensioned the array with.

#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/file.h"
#include "sys/inode.h"
#include "sys/kctl.h"
#include "sys/map.h"
#include "sys/mount.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/systm.h"
#include "sys/text.h"
#include "sys/tty.h"
#include "sys/types.h"
#include "sys/user.h"

// The two Consul typewriters.  No header declares them; kernel/conf.c declares its own.
extern struct tty sc[];

struct ksym {
    char ks_name[KSYMLEN]; // NUL-padded; a name is at most KSYMLEN-1 characters
    void *ks_addr;         // FAT -- read it through ptrword()
    int ks_size;           // bytes
};

// The exported set.  The comment column is re-checked whenever a program arrives, and has
// twice been wrong (cmd/TODO.md task C8): "who reads this" must have an answer.
//
// WHAT IS DELIBERATELY ABSENT, since an empty answer looks like an unasked question:
//
//   dk_busy -- live, but an INSTANT and not a count, and no interval divides it.  dk_time's
//     low three bits are it sampled every tick, so a percentage falls out of the histogram.
//   u, buffers -- ABSOLUTE symbols; UBASE and BUFBASE are already in <sys/param.h>.
//   NPROC, NTEXT, NFILE, NINODE, NMOUNT, NSC, MSGBUFS, HZ, NDK -- <sys/param.h> constants,
//     shared with user space.  RetroBSD's table carries _nproc and _hz because its userland
//     could not see them; ours can, and that is the largest simplification against it.
//   runin/runout/runrun/curpri; mpid; callout; cfree; buf; bfreelist; panicstr; phymem;
//     maxmem; nblkdev; nchrdev; bdevsw; cdevsw; rootdev; pipedev -- live, and no reader.
//     vmstat was the program they were kept for and wants none of them.
static struct ksym ksym[] = {
    { "proc", proc, NPROC * sizeof(struct proc) },     // ps  pstat
    { "text", text, NTEXT * sizeof(struct text) },     //     pstat
    { "inode", inode, NINODE * sizeof(struct inode) }, //     pstat
    { "file", file, NFILE * sizeof(struct file) },     //     pstat
    { "mount", mount, sizeof mount },                  //     pstat
    { "sc", sc, NSC * sizeof(struct tty) },            //     pstat -- the only tty array
    { "uhome", &uhome, sizeof uhome },                 //     pstat -u -- whose u-area is live
    { "lbolt", &lbolt, sizeof lbolt },                 //     pstat -c
    { "time", &time, sizeof time },                    //     pstat -c

    { "msgbuf", msgbuf, sizeof msgbuf },     // dmesg
    { "msgbufp", &msgbufp, sizeof msgbufp }, // dmesg -- a FAT char *; ptrword() it

    { "coremap", coremap, sizeof coremap },  //     pstat
    { "swapmap", swapmap, sizeof swapmap },  //     pstat
    { "nswap", &nswap, sizeof nswap },       //     pstat
    { "swplo", &swplo, sizeof swplo },       //     pstat
    { "swapdev", &swapdev, sizeof swapdev }, //     pstat

    { "dk_time", dk_time, sizeof dk_time },  //            iostat  vmstat
    { "dk_numb", dk_numb, sizeof dk_numb },  //            iostat  vmstat
    { "dk_wds", dk_wds, sizeof dk_wds },     //            iostat
    { "tk_nin", &tk_nin, sizeof tk_nin },    //            iostat
    { "tk_nout", &tk_nout, sizeof tk_nout }, //            iostat

    // The rate counters vmstat divides by an interval.
    { "nintr", &nintr, sizeof nintr },          //                 vmstat
    { "nsyscall", &nsyscall, sizeof nsyscall }, //                 vmstat
    { "ntrap", &ntrap, sizeof ntrap },          //                 vmstat -p, vmstat -s
    { "nswtch", &nswtch, sizeof nswtch },       //                 vmstat
    { "nswapin", &nswapin, sizeof nswapin },    //                 vmstat -p, vmstat -s
    { "nswapout", &nswapout, sizeof nswapout }, //                 vmstat -p, vmstat -s

    { "ntextin", &ntextin, sizeof ntextin },       //              vmstat -s
    { "ntextout", &ntextout, sizeof ntextout },    //              vmstat -s
    { "ntextjoin", &ntextjoin, sizeof ntextjoin }, //              vmstat -s
    { "niobulk", &niobulk, sizeof niobulk },       //              vmstat -s
    { "nioedge", &nioedge, sizeof nioedge },       //              vmstat -s
    { "nioshift", &nioshift, sizeof nioshift },    //              vmstat -s
};

#define NKSYM ((int)(sizeof ksym / sizeof ksym[0]))

// Copy the name in.  There is no copyinstr(); fubyte() is the only byte-granular read of
// user space.  Copy ONCE and compare in kernel memory afterwards: comparing out of user
// space would be hundreds of validated byte reads per lookup and would let the caller aim an
// unbounded compare where it liked.  u.u_dirp is already the first argument.
//
// THE PADDING IS NOT DECORATION: ksymfind() compares the whole KSYMLEN-byte field against a
// NUL-padded table, so a garbage tail makes every lookup fail with ENOENT -- which looks
// exactly like a working call on an unknown name.
static int ksymname(register char *kn)
{
    register caddr_t cp = u.u_dirp;
    register int i, c;

    for (i = 0; i < KSYMLEN; i++) {
        if ((c = fubyte(cp++)) < 0) {
            u.u_error = EFAULT;
            return -1;
        }
        kn[i] = c;
        if (c == 0) {
            while (++i < KSYMLEN)
                kn[i] = 0;
            return 0;
        }
    }
    // No NUL in KSYMLEN bytes: malformed, not merely unknown, so EINVAL and not ENOENT.
    u.u_error = EINVAL;
    return -1;
}

// The row named by kn, or -1.  The kernel has no strcmp; both sides are NUL-padded, so a
// bounded run over the whole field is an exact match test.
static int ksymfind(register char *kn)
{
    register int i, j;

    for (i = 0; i < NKSYM; i++) {
        for (j = 0; j < KSYMLEN; j++)
            if (kn[j] != ksym[i].ks_name[j])
                goto next;
        return i;
    next:;
    }
    return -1;
}

// Hand `n' bytes at kernel address `src' over and report how many arrived -- read(2)'s rule
// and the only rule this call has.  copyoutb() and not copyout(): the latter is word-only
// and msgbuf's caller need not be word-phased.
static void ksymout(caddr_t src, int n, caddr_t buf, int len)
{
    if (len <= 0) {
        u.u_r.r_val1 = n;
        return;
    }
    n = min(n, len);
    if (copyoutb(src, buf, n) < 0) {
        u.u_error = EFAULT;
        return;
    }
    u.u_r.r_val1 = n;
}

// KCTL_LIST: the names, KSYMLEN bytes each, NUL-padded.  ONE copyoutb PER NAME: a row is
// four words, so a single run from ksym[0].ks_name would interleave addresses and sizes with
// the names at a length that looks exactly right.
static void ksymlist(caddr_t buf, int len)
{
    register int i, n, k;

    n = NKSYM * KSYMLEN;
    if (len <= 0) {
        u.u_r.r_val1 = n;
        return;
    }
    n = min(n, len);
    for (i = 0; i < n; i += KSYMLEN) {
        k = min(KSYMLEN, n - i); // the last record may be clipped by a short len
        if (copyoutb(ksym[i / KSYMLEN].ks_name, buf + i, k) < 0) {
            u.u_error = EFAULT;
            return;
        }
    }
    u.u_r.r_val1 = n;
}

//
// KCTL_PSINFO -- the u-area half of the process table, for ps(1).
//
// It is an OPERATION and not a row because a digest computed at the moment of asking has no
// address to relocate.  <sys/kctl.h> is the account of why it exists; what it buys is that
// ps opens no memory device and needs no privilege.
//
// THE U-AREA IS IN ONE OF THREE PLACES, and getting it wrong prints numbers a context switch
// old.  p_addr == uhome: the LIVE u-area at UBASE is that process's and the copy at p_addr is
// stale (kernel/text.c), so read `u' directly -- the kernel runs unmapped.  SLOAD set and
// p_addr != uhome: the image is in core, its first USIZE words the saved u-area, at or above
// KREACH, so copyphys() as kernel/dev/mem.c does.  SLOAD clear: swapped out, p_addr is a
// block on the paging store; the row comes back empty and ps prints <swapped>.
//
// A ZOMBIE IS NOT READ AT ALL AND THE TEST COMES FIRST: exit() calls mfree(coremap, ...) and
// only THEN sets SZOMB (kernel/sys1.c), so its p_addr names core already handed back.
//
// All int, as kernel/dev/mem.c is: an unsigned compare or add here is an out-of-line call.

// The bounce buffer.  Static rather than a kernel-stack array, and small because the three
// runs below are two words, one and btow(DIRSIZ).
#define UPEEKMAX 8
static int upeekbuf[UPEEKMAX];

// Word offsets inside the u-area, taken from the LIVE one rather than written down: a saved
// u-area has `struct user' at word 0 of the image, so renaming a field breaks the build
// instead of silently moving a column.
#define UOFF(f) (ptrword((caddr_t)&u.f) - ptrword((caddr_t)&u))

// `n' words at physical word address `src' into `dst'.  copyphys() opens one window per side
// and neither run may cross a page; the SOURCE never can (USIZE is one page and p_addr is
// page-aligned), so the loop is for the destination alone.
static void upeek(int src, int *dst, int n)
{
    int w, i, off;

    while (n > 0) {
        off = ptrword(upeekbuf) & (PGSZ - 1);
        w   = min(n, PGSZ - off);
        copyphys(src, ptrword(upeekbuf), w);
        for (i = 0; i < w; i++)
            dst[i] = upeekbuf[i];
        src += w;
        dst += w;
        n -= w;
    }
}

// One row, with the u-area columns left empty when there is no u-area to be had.
static void psfill(register struct proc *p, register struct psinfo *ps)
{
    struct tty *tp;
    time_t ut[2]; // u_utime and u_stime, adjacent in <sys/user.h> and read as one run
    int i;

    ps->ps_pid  = p->p_pid;
    ps->ps_time = 0;
    ps->ps_ttyn = -1;
    for (i = 0; i < DIRSIZ; i++)
        ps->ps_comm[i] = 0;

    if (p->p_stat == 0 || p->p_stat == SZOMB || (p->p_flag & SLOAD) == 0)
        return;

    if (p->p_addr == (paddr_t)uhome) {
        ut[0] = u.u_utime;
        ut[1] = u.u_stime;
        tp    = u.u_ttyp;
        for (i = 0; i < DIRSIZ; i++)
            ps->ps_comm[i] = u.u_comm[i];
    } else {
        upeek((int)p->p_addr + UOFF(u_utime), (int *)ut, 2);
        upeek((int)p->p_addr + UOFF(u_ttyp), (int *)&tp, 1);
        upeek((int)p->p_addr + UOFF(u_comm), (int *)ps->ps_comm, btow(DIRSIZ));
    }
    ps->ps_time = ut[0] + ut[1];

    // The terminal's name is the index of u_ttyp in sc[] -- that array is the only one in
    // this kernel and sc[minor(dev)] is how the driver itself indexes it, so the subscript
    // is the answer and costs no directory scan.  The stride is computed, never written.
    if (tp != NULL) {
        i = (ptrword((caddr_t)tp) - ptrword((caddr_t)sc)) / (int)(sizeof(struct tty) / NBPW);
        if (i >= 0 && i < NSC)
            ps->ps_ttyn = i;
    }
}

// NPROC records in proc[]'s order, so a caller's two calls line up by index.  ksymout()'s
// rule, and ONE copyout PER SLOT: there is no array here, and the whole point is that the
// kernel never holds NPROC of these.
static void ksympsinfo(caddr_t buf, int len)
{
    struct psinfo ps;
    register int i, n, k, sz;

    sz = (int)sizeof ps;
    n  = NPROC * sz;
    if (len <= 0) {
        u.u_r.r_val1 = n;
        return;
    }
    n = min(n, len);
    for (i = 0; i < n; i += sz) {
        psfill(&proc[i / sz], &ps);
        k = min(sz, n - i); // the last record may be clipped by a short len
        if (copyout((caddr_t)&ps, buf + i, k) < 0) {
            u.u_error = EFAULT;
            return;
        }
    }
    u.u_r.r_val1 = n;
}

// int kctl(const char *name, int op, void *buf, int len)
void kctl()
{
    register struct a {
        char *name;
        int op;
        caddr_t buf;
        int len;
    } *uap = (struct a *)u.u_ap;
    struct kctlstat st;
    char kn[KSYMLEN];
    register int i;

    // THE TWO OPERATIONS THAT NAME NOTHING COME FIRST, before the name is touched: a caller
    // may pass anything there -- a null pointer included -- and reading it would turn a
    // legal call into an EFAULT.
    if (uap->op == KCTL_LIST) {
        ksymlist(uap->buf, uap->len);
        return;
    }
    if (uap->op == KCTL_PSINFO) {
        ksympsinfo(uap->buf, uap->len);
        return;
    }

    if (ksymname(kn) < 0)
        return;
    if ((i = ksymfind(kn)) < 0) {
        u.u_error = ENOENT;
        return;
    }

    switch (uap->op) {
    case KCTL_GET:
        // ptrword() strips the fat marker the initializer put there; the (caddr_t)(int *)
        // pair rebuilds one at byte 0 of that word, as kernel/dev/mem.c does.
        ksymout((caddr_t)(int *)ptrword(ksym[i].ks_addr), ksym[i].ks_size, uap->buf, uap->len);
        return;

    case KCTL_STAT:
        st.kc_addr  = ptrword(ksym[i].ks_addr);
        st.kc_size  = ksym[i].ks_size;
        st.kc_flags = KCTLF_RD;
        ksymout((caddr_t)&st, sizeof st, uap->buf, uap->len);
        return;

    default:
        // KCTL_SET lands here with everything else: reserved, not refused on its own
        // account.  Implementing it wants a per-entry KCTLF_WR and a suser() gate.
        u.u_error = EINVAL;
        return;
    }
}
