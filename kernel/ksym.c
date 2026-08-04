// The kernel-variable table, and kctl(2) over it.  <sys/kctl.h> is the interface and says
// why this call exists at all; this file is the table and the handler.
//
// THE TABLE IS NOT THE KERNEL'S SYMBOL TABLE.  It is the set of variables the inspection
// programs ask for, and every row names the program that asks.  A row with nothing in its
// right-hand column does not belong here: an exported variable is a promise, and a promise
// nobody wanted is one to keep for nothing.
//
// It cannot drift from the image it is part of.  Each address is a link-time relocation of
// the REAL declaration, pulled in through the headers below -- so renaming or deleting one of
// these variables stops this file compiling rather than leaving a table pointing at rubble.
// That is the whole reason the headers are included instead of the symbols being re-declared
// locally, and it is what a /unix on the disk could never have promised: an image and a
// running kernel that disagree is the classic nlist(3) failure, and it is unreachable here by
// construction.
//
// ks_addr IS A void * AND THAT IS FORCED, NOT CHOSEN.  A static initializer on this compiler
// folds an address constant from &lvalue, from an array or function name, and from
// address ± constant -- and from nothing else: eval_addr_const() (the c-compiler's
// semantic/initializers.c) has no case for a CAST, so `(int)proc' and `(int *)proc' both fail
// to compile.  What is left is a bare object name, and the type check that follows admits one
// only when the field's pointee is void or compatible.  So a void * field is the only one
// that takes proc, &time, msgbuf and sc with no cast anywhere.  It is stored as a FAT pointer
// (marker in bit 48, byte offset in bits 47-45; &c on a standalone char even gets offset 5),
// so every read of ks_addr goes through ptrword().  bufpaddr() in <sys/buf.h> does the same
// thing to a caddr_t; doc/Besm6_Data_Representation.md section 7 is the general account.

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

// The two Consul typewriters.  No header declares them -- kernel/conf.c declares its own for
// the same reason, dev/sc.c being the only terminal driver.  NSC is <sys/param.h>'s, and it
// is there rather than in the driver precisely so that this row and the program reading it
// can agree about how long the array is.
extern struct tty sc[];

struct ksym {
    char ks_name[KSYMLEN]; // NUL-padded; a name is at most KSYMLEN-1 characters
    void *ks_addr;         // FAT -- read it through ptrword()
    int ks_size;           // bytes
};

// The exported set.
//
// THE COMMENT COLUMN WAS RE-CHECKED WHEN THE PROGRAMS ARRIVED (cmd/TODO.md task C8), and
// three rows had claimed a reader they turned out not to have.  `text' said ps: ps has no
// TEXTP column and never grew one, so it says pstat alone now.  `lbolt' and `time' said ps
// too, and ps has neither a START nor an ELAPSED column; rather than delete two live and
// useful variables, pstat grew a -c that prints the system clock to sub-second resolution,
// which time(2) cannot report and nothing else in userland can see.  That is the discipline
// working as intended: the question "who reads this" has to have an answer, and the answer
// may be a feature nobody had written yet.
//
// RE-CHECKED AGAIN WHEN vmstat ARRIVED, and this time nothing had to be corrected: dk_time
// picked up a second reader and the iostat-only rows stayed iostat-only.  What did change is
// that the closing paragraph below stopped being a promise -- the counters it was holding
// open for a vmstat are in the table now, and the ones vmstat turned out not to want are
// still out of it, with the reason written down rather than the wait.
//
// A SIZE IS NEVER A HAND-WRITTEN NUMBER: it is either sizeof of the object, where the extern
// carries its bound, or the same COUNT MACRO kernel/conf.c dimensioned the array with.  Four
// of these -- proc, text, inode, file -- are declared `extern struct x x[]' with no bound in
// their headers, so sizeof does not apply to them and the count has to be named; naming the
// macro rather than a literal is what keeps a retuned NPROC carrying this table with it.
//
// WHAT IS DELIBERATELY ABSENT, since an empty answer looks the same as an unasked question:
//
//   dk_busy -- live now (dev/md.c and dev/mb.c keep it, a bit each; NDK in <sys/param.h>),
//     and STILL absent, for a different reason than the one it used to have.  It is an
//     INSTANT and not a count: what it says is what the drives happened to be doing during
//     the read, and no interval divides it.  What a report wants is dk_time, whose low three
//     bits ARE dk_busy sampled at every tick -- so a per-device percentage falls out of the
//     histogram and needs nothing else.  dk_numb and dk_wds ARE here: those are counts, and
//     iostat and vmstat divide them by an interval.  There is no cp_time on this system;
//     dk_time is it, and kernel/clock.c stamps 0 user, 8 nice, 16 system, 24 idle, each of
//     those a BASE and not a slot.
//   u, buffers -- ABSOLUTE symbols (kernel/besm6.S), and UBASE and BUFBASE are already in
//     <sys/param.h>, which user code includes.  lib/test/memt.c reads the u-area with no
//     lookup at all.  A second route to a constant is a second thing to keep true.
//   NPROC, NTEXT, NFILE, NINODE, NMOUNT, NSC, MSGBUFS, HZ, NDK -- <sys/param.h> constants,
//     shared with user space.  There is no `nproc' variable to export and there must not be
//     one; this is the largest simplification against RetroBSD, whose table carries _nproc,
//     _hz, _cnttys and _dk_ndrive because its userland could not see the constants.  vmstat
//     needs no boottime row either: the ticks in dk_time ARE the uptime, and using them puts
//     every number in a report over the same denominator.
//   runin/runout/runrun/curpri; mpid; callout; cfree; buf; bfreelist; panicstr; phymem;
//     maxmem; nblkdev; nchrdev; bdevsw; cdevsw; rootdev; pipedev -- live and real, and no
//     program asks.  VMSTAT WAS THE PROGRAM THEY WERE BEING KEPT FOR, it is here now
//     (cmd/vmstat), and it wants none of them: `procs r/b/w' is a scan of proc[] and `fre' a
//     sum over coremap, both of which this table already carries.  The swapper, text and
//     iomove counters DID come with it, and are below.
static struct ksym ksym[] = {
    { "proc", proc, NPROC * sizeof(struct proc) },     // ps  pstat
    { "text", text, NTEXT * sizeof(struct text) },     //     pstat
    { "inode", inode, NINODE * sizeof(struct inode) }, //    pstat
    { "file", file, NFILE * sizeof(struct file) },     //     pstat
    { "mount", mount, sizeof mount },                  //     pstat
    { "sc", sc, NSC * sizeof(struct tty) },            // ps  pstat -- the only tty array
    { "uhome", &uhome, sizeof uhome },                 // ps  -- whose u-area is live at UBASE
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

    // The rate counters vmstat divides by an interval.  Six were here in spirit already --
    // the block comment above used to say they came with a vmstat -- and four are new
    // (kernel/intr.c, syscall.c, trap.c, slp.c).
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

// Copy the name in.  There is no copyinstr() on this system, and there is no wanting one for
// a single caller: fubyte() is the only byte-granular read of user space (kernel/usermem.S),
// and uchar() in kernel/nami.c already walks a pathname with it.
//
// Copy ONCE and compare in kernel memory afterwards.  Comparing straight out of user space --
// which is what RetroBSD's node does, its part having no MMU -- would be one validated byte
// read per character per table row, some hundreds per lookup, and would let the caller aim an
// unbounded compare at an address of its choosing.
//
// u.u_dirp is the first argument and is already set for us (kernel/syscall.c): the (caddr_t)
// cast of u_arg[0] there is a silent COPY, so the fat marker and byte offset the caller built
// survive it.  Returns 0, or -1 with u_error set.
//
// THE PADDING IS NOT DECORATION.  The rest of the buffer is zeroed once the NUL arrives,
// because ksymfind() compares the whole KSYMLEN-byte field and the table's side is
// NUL-padded by its own initializer.  Leaving the tail as whatever was on the kernel stack
// makes every lookup fail -- and fail in the one way that looks like a working call, since
// ENOENT is exactly what an unknown name gets.
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
    // No NUL in KSYMLEN bytes.  It cannot name anything in the table -- every entry's name is
    // shorter than that -- but say EINVAL rather than ENOENT: the argument is malformed, not
    // merely unknown, and a caller that has run two strings together should hear about it.
    u.u_error = EINVAL;
    return -1;
}

// The row named by kn, or -1.  The kernel has no strcmp -- it links -lruntime alone
// (kernel/README.md) -- and one bounded loop is cheaper than the reason to add one.  Both
// sides are NUL-padded to KSYMLEN, so a byte-for-byte run over the whole field is an exact
// match test and needs no length.
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

// Hand `n' bytes at kernel address `src' to the caller and report how many arrived.  This is
// read(2)'s rule and the only rule this call has: `len' of 0 copies nothing and answers with
// the size AVAILABLE, so a caller can size a buffer; a short `len' truncates rather than
// failing, so completeness is a comparison the caller makes, exactly as after a read().
//
// copyoutb() and not copyout(): copyout() is word-only and masks the byte offsets away
// (<sys/systm.h>), and msgbuf is a char array whose caller's buffer need not be word-phased.
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

// KCTL_LIST: the names, KSYMLEN bytes each, NUL-padded.
//
// ONE copyoutb PER NAME, and not one run over the table.  A row is four words -- two of name
// and one each of address and size -- so a single copy from ksym[0].ks_name would hand the
// caller the addresses and sizes interleaved with the names, at a length that looks exactly
// right.  Nothing about the return value would say so; lib/test/kctlt's walk over the list is
// what does, which is why it stats every name it finds rather than counting them.
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

    // KCTL_LIST first, and before the name is touched: it names nothing, so a caller may pass
    // anything at all there -- a null pointer included -- and reading it would turn a legal
    // call into an EFAULT.  The names go out as fixed-width records, so a caller divides by
    // KSYMLEN instead of scanning for separators.
    if (uap->op == KCTL_LIST) {
        ksymlist(uap->buf, uap->len);
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
        // ptrword() strips the fat marker and byte offset the initializer put there; the
        // (caddr_t)(int *) pair then builds a fat pointer at byte 0 of that word, which is
        // the conversion kernel/dev/mem.c makes for the same reason and describes in full.
        ksymout((caddr_t)(int *)ptrword(ksym[i].ks_addr), ksym[i].ks_size, uap->buf, uap->len);
        return;

    case KCTL_STAT:
        st.kc_addr  = ptrword(ksym[i].ks_addr);
        st.kc_size  = ksym[i].ks_size;
        st.kc_flags = KCTLF_RD;
        ksymout((caddr_t)&st, sizeof st, uap->buf, uap->len);
        return;

    default:
        // KCTL_SET lands here with everything else.  It is reserved rather than refused on
        // its own account: nothing needs to write a kernel variable yet, and when something
        // does it wants a per-entry KCTLF_WR and a suser() gate, neither of which is here.
        u.u_error = EINVAL;
        return;
    }
}
