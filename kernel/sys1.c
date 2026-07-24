// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/buf.h"
#include "sys/reg.h"
#include "sys/inode.h"
#include "sys/seg.h"
#include "sys/acct.h"
// clang-format on

// exec system call, with and without environments.
struct execa {
    char *fname;
    char **argp;
    char **envp;
};

void setregs(void);

void exec()
{
    ((struct execa *)u.u_ap)->envp = NULL;
    exece();
}

// Announce a failed exec that nobody else can report.  A caller with a stdout prints its
// own message; process 1 at boot has none -- it inherits proc[0]'s empty u_ofile[] -- so
// the icode's exec of /etc/init (kernel/besm6.S) would otherwise fail into a silent spin.
// Called from both of exece()'s exits, and quiet unless a call actually failed.
static void execerr(void)
{
    char name[DIRSIZ + 1];

    if (u.u_error == 0 || u.u_ofile[1] != NULL)
        return;
    // u_dbuf is the last path component namei() looked at -- "init" for /etc/init -- and is
    // NUL-padded only when the component is shorter than DIRSIZ, so copy and terminate.
    // DIRSIZ is three words exactly (param.h), which is what lets wcopy() do the copy.
    wcopy((caddr_t)u.u_dbuf, (caddr_t)name, btow(DIRSIZ));
    name[DIRSIZ] = '\0';
    printf("exec %s: error %d\n", name, u.u_error);
}

void exece()
{
    register int nc;
    register char *cp;
    register struct buf *bp;
    register struct execa *uap;
    // The byte cursor, reused by the two loops below the way `cp' is: the caller's string
    // while the arguments are staged, the new image's while they are copied back.  It is a
    // real char * -- a FAT pointer -- so that `up++' is the compiler's own byte step
    // (b$pinc: walk the offset field 5 -> 0, then carry into the word address).
    char *up;
    int na, ne, bno, ucp, ap, c;
    struct inode *ip;

    if ((ip = namei(uchar, 0)) == NULL) {
        execerr(); // the boot case: no such file, and no stdout to say so on
        return;
    }
    bno = 0;
    bp  = 0;
    if (access(ip, IEXEC))
        goto bad;
    if ((ip->i_mode & IFMT) != IFREG || (ip->i_mode & (IEXEC | (IEXEC >> 3) | (IEXEC >> 6))) == 0) {
        u.u_error = EACCES;
        goto bad;
    }
    // Collect arguments on "file" in swap space.
    na  = 0;
    ne  = 0;
    nc  = 0;
    uap = (struct execa *)u.u_ap;
    if ((bno = malloc(swapmap, (NCARGS + BSIZE - 1) / BSIZE)) == 0)
        panic("Out of swap");
    if (uap->argp)
        for (;;) {
            ap = NULL;
            if (uap->argp) {
                ap = fuword((caddr_t)uap->argp);
                uap->argp++;
            }
            if (ap == NULL && uap->envp) {
                uap->argp = NULL;
                if ((ap = fuword((caddr_t)uap->envp)) == NULL)
                    break;
                uap->envp++;
                ne++;
            }
            if (ap == NULL)
                break;
            na++;
            if (ap == -1)
                u.u_error = EFAULT;
            // `ap' is the CALLER's own char *, and on this machine that is a fat pointer:
            // marker in bit 48, byte offset in bits 47-45.  Walking it means walking that
            // offset, which only pointer arithmetic does -- `ap++' on the int would step
            // the WORD address and read one byte in six (and, with bits 48-42 non-zero,
            // would be an ADD whose left operand the additive unit reads as an exponent).
            // The int -> char * conversion below is a bit copy, so the caller's marker and
            // offset survive it intact.
            up = (char *)ap;
            do {
                if (nc >= NCARGS - 1)
                    u.u_error = E2BIG;
                if ((c = fubyte(up++)) < 0)
                    u.u_error = EFAULT;
                if (u.u_error)
                    goto bad;
                // `nc' counts BYTES, and a block is BSIZE == 3072 of them -- not a
                // power of two, so this is a remainder and a divide.  Once per 3072
                // characters of arg list, against the getblk() it guards.
                if (nc % BSIZE == 0) {
                    if (bp)
                        bawrite(bp);
                    bp = getblk(swapdev, swplo + bno + nc / BSIZE);
                    // b_addr is a word pointer; the cast makes a byte cursor at the
                    // block's first byte (offset 5), which is what `*cp++' walks.
                    cp = (caddr_t)bp->b_addr;
                }
                nc++;
                *cp++ = c;
            } while (c > 0);
        }
    if (bp)
        bawrite(bp);
    bp = 0;
    nc = (nc + NBPW - 1) & ~(NBPW - 1);
    // The stack must hold the pointer vector as well as the strings: argc, na pointers,
    // the two NULLs and the terminating word.
    if (getxfile(ip, nc + (na + 4) * NBPW) || u.u_error)
        goto bad;

    // Copy the arglist back into the new image, at the BASE of the user stack.
    //
    // The BESM-6 stack grows UP from USTKPAGE * PGSZ = 070000, so the block sits at that
    // fixed base and r15 starts ABOVE it -- the program's own stack growth can never walk
    // back over its own arguments.  argc is therefore always at absolute 070000, which is
    // how a crt0 finds it with no register hand-off.
    //
    //      070000  argc
    //              argv[0] .. argv[argc-1]     (FAT pointers to the strings)
    //              0
    //              envp[0] .. envp[ne-1]
    //              0
    //              the strings, byte-packed six to a word
    //        r15 = the first free word above the block
    //
    // Two units meet here, and only the vector's is unusual.  suword() takes a WORD address
    // -- it masks its caddr_t to the low 15 bits (usermem.S) -- so the pointer vector strides
    // by ONE, not by NBPW; a stride of NBPW would skip six words per pointer.  Everything
    // byte-granular is an ordinary char *: the cursor `up', and the VALUES stored in the
    // vector, which are that same cursor.  A char * is a fat pointer -- marker in bit 48,
    // byte offset in bits 47-45 as a right-shift distance, 5 naming the word's first byte --
    // and a plain word address is not one: it asks `asx' for a shift of -64, so the user's
    // first dereference of argv[0] would read zero.
    //
    // This used to be an explicit (word, offset) pair, and it was wrong twice over.  It
    // started at offset 0 and counted UP, but offset 0 is byte #5, the word's LAST; the
    // strings therefore went down LSB-first, the reverse of how six chars pack into a word
    // and of how the compiler's own ++ walks them.  And no fixed offset could have saved it:
    // only the first string starts on a word boundary, the rest beginning wherever the
    // previous one's NUL left off.
    ap           = USTKPAGE * PGSZ;     // argc, then the pointer vector
    ucp          = ap + 1 + na + 2;     // the strings, just above the vector
    up           = (caddr_t)(int *)ucp; // ... as a char *: byte #0 of that word
    u.u_ar0[R15] = ap;                  // provisional; the real value is set below
    suword((caddr_t)ap, na - ne);
    nc = 0;
    for (;;) {
        ap++;
        if (na == ne) {
            suword((caddr_t)ap, 0);
            ap++;
        }
        if (--na < 0)
            break;
        suword((caddr_t)ap, (int)up); // argv[i] / envp[i]: the fat pointer itself
        do {
            // `nc' counts bytes; see the matching staging loop above.
            if (nc % BSIZE == 0) {
                if (bp)
                    brelse(bp);
                bp = bread(swapdev, swplo + bno + nc / BSIZE);
                cp = (caddr_t)bp->b_addr; // byte cursor; see the staging loop above
            }
            subyte(up++, (c = *cp++));
            nc++;
        } while (c & 0377);
    }
    suword((caddr_t)ap, 0);
    // round the cursor up: the closing word must not eat a partial string
    ucp = ptrword(up);
    if (ptrbyte(up) != 5)
        ucp++;
    suword((caddr_t)ucp, 0);
    u.u_ar0[R15] = ucp + 1; // the first free word; setregs() does not touch r15
    setregs();
bad:
    if (bp)
        brelse(bp);
    if (bno)
        mfree(swapmap, (NCARGS + BSIZE - 1) / BSIZE, bno);
    iput(ip);
    execerr(); // the success path lands here too; execerr() tests u_error itself
}

// Read in and set up memory for executed file.
// Zero return is normal;
// non-zero means only the text is being replaced
int getxfile(register struct inode *ip, int nargc)
{
    register int ds;
    register int ts, ss;
    register int i;
    int lsize;

    // read in the 8-word BESM-6 a.out header (cross/besm6/b.out.h) for the
    // segment sizes.  sizeof(u.u_exdata) == HDRSZ == BADDR words, so this reads
    // exactly the header and leaves the file cursor at the const segment.
    //  ux_mag = FMAGIC  impure: one writable region from word BADDR
    //         = NMAGIC  pure:   read-only const+text, page-aligned data

    u.u_base   = (caddr_t)&u.u_exdata;
    u.u_count  = sizeof(u.u_exdata);
    u.u_offset = 0;
    u.u_segflg = 1;
    readi(ip);
    u.u_segflg = 0;
    if (u.u_error)
        goto bad;
    if (u.u_count != 0) {
        u.u_error = ENOEXEC;
        goto bad;
    }
    // The segment sizes come straight off the disk, so they are hostile input.
    // v7 caught a too-large header by letting a 32-bit sum overflow a 16-bit
    // field and comparing; here every field is one 41-bit word, so that
    // comparison can never fire and the check had quietly gone dead.  Test the
    // sizes directly instead -- a negative one would sail through btow() and
    // pground() and come out as a nonsense map.
    if (u.u_exdata.ux_csize < 0 || u.u_exdata.ux_tsize < 0 || u.u_exdata.ux_dsize < 0 ||
        u.u_exdata.ux_bsize < 0) {
        u.u_error = ENOEXEC;
        goto bad;
    }
    if (u.u_exdata.ux_mag == FMAGIC) {
        // Impure: const+text share the single writable region with data.  Fold
        // them into ux_dsize and zero the read-only sizes, so ts comes out 0 and
        // xalloc() (no shared text) is skipped.
        lsize = u.u_exdata.ux_csize + u.u_exdata.ux_tsize + u.u_exdata.ux_dsize;
        if (lsize < 0) { // sum overflowed 41 bits
            u.u_error = ENOMEM;
            goto bad;
        }
        u.u_exdata.ux_dsize = lsize;
        u.u_exdata.ux_csize = 0;
        u.u_exdata.ux_tsize = 0;
    } else if (u.u_exdata.ux_mag != NMAGIC) {
        u.u_error = ENOEXEC;
        goto bad;
    }
    if (u.u_exdata.ux_tsize != 0 && (ip->i_flag & ITEXT) == 0 && ip->i_count != 1) {
        u.u_error = ETXTBSY;
        goto bad;
    }

    // find text and data sizes
    // try them out for possible
    // overflow of max sizes
    //
    // The read-only region (NMAGIC) is the header hole + const + text, so ts
    // carries the BADDR-word hole; the image begins at word BADDR (cross/besm6/
    // b.out.h).  Under FMAGIC ux_tsize is 0, so ts is 0 and the hole falls into
    // the data region instead -- `ts ? 0 : BADDR' below adds it there.
    ts    = u.u_exdata.ux_tsize
                ? pground(BADDR + btow(u.u_exdata.ux_csize + u.u_exdata.ux_tsize))
                : 0;
    lsize = u.u_exdata.ux_dsize + u.u_exdata.ux_bsize;
    if (lsize < 0) { // sum overflowed 41 bits
        u.u_error = ENOMEM;
        goto bad;
    }
    ds = pground(btow(lsize) + (ts ? 0 : BADDR));
    ss = SSIZE + pground(btow(nargc));
    if (estabur(ts, ds, ss, 0, RO))
        goto bad;

    // allocate and clear core
    // at this point, committed
    // to the new image

    u.u_prof.pr_scale = 0;
    xfree();
    i = USIZE + ds + ss;
    expand(i);
    while ((i -= PGSZ) >= USIZE)
        clearseg(u.u_procp->p_addr + i);
    xalloc(ip);

    // read in data segment
    //
    // const-origin-relative: the data image sits in the file just past the header
    // hole + const + text, and estabur(0,ds,0) remaps the data region to virtual 0
    // for the read.  Under FMAGIC (ts == 0) the folded blob starts at word BADDR,
    // so it must land at word BADDR, skipping the header hole -- (caddr_t)(int*)BADDR
    // is the fat pointer to byte #0 of that word (mmutest check 25).  Under NMAGIC
    // ux_csize/ux_tsize are the real const/text sizes and the base is virtual 0.

    estabur(0, ds, 0, 0, RO);
    u.u_base   = ts ? (caddr_t)0 : (caddr_t)(int *)BADDR;
    u.u_offset = sizeof(u.u_exdata) + u.u_exdata.ux_csize + u.u_exdata.ux_tsize;
    u.u_count  = u.u_exdata.ux_dsize;
    readi(ip);
    // set SUID/SGID protections, if no tracing
    if ((u.u_procp->p_flag & STRC) == 0) {
        if (ip->i_mode & ISUID)
            if (u.u_uid != 0) {
                u.u_uid          = ip->i_uid;
                u.u_procp->p_uid = ip->i_uid;
            }
        if (ip->i_mode & ISGID)
            u.u_gid = ip->i_gid;
    } else
        psignal(u.u_procp, SIGTRC);
    u.u_tsize = ts;
    u.u_dsize = ds;
    u.u_ssize = ss;
    estabur(ts, ds, ss, 0, RO);
bad:
    return (0);
}

// Clear registers on exec
void setregs()
{
    register int *rp;
    register char *cp;
    register int i;

    for (rp = &u.u_signal[0]; rp < &u.u_signal[NSIG]; rp++)
        if ((*rp & 1) == 0)
            *rp = 0;
    for (cp = &regloc[0]; cp < &regloc[15];)
        u.u_ar0[*cp++] = 0;
    // exec is an extracode: the new image starts via `выпр' through ERET, held in RET.
    u.u_ar0[RET] = u.u_exdata.ux_entloc;
    for (i = 0; i < NOFILE; i++) {
        if (u.u_pofile[i] & EXCLOSE) {
            closef(u.u_ofile[i]);
            u.u_ofile[i] = NULL;
            u.u_pofile[i] &= ~EXCLOSE;
        }
    }
    // Remember file name for accounting.
    u.u_acflag &= ~AFORK;
    wcopy((caddr_t)u.u_dbuf, (caddr_t)u.u_comm, btow(DIRSIZ));
}

// exit system call:
// pass back caller's arg
void rexit()
{
    register struct a {
        int rval;
    } *uap;

    uap = (struct a *)u.u_ap;
    exit((uap->rval & 0377) << 8);
}

// Release resources.
// Save u. area for parent to look at.
// Enter zombie state.
// Wake up parent and init processes,
// and dispose of children.
void exit(int rv)
{
    register int i;
    register struct proc *p, *q;
    register struct file *f;

    p = u.u_procp;
    p->p_flag &= ~(STRC | SULOCK);
    p->p_clktim = 0;
    for (i = 0; i < NSIG; i++)
        u.u_signal[i] = 1;
    for (i = 0; i < NOFILE; i++) {
        f            = u.u_ofile[i];
        u.u_ofile[i] = NULL;
        closef(f);
    }
    plock(u.u_cdir);
    iput(u.u_cdir);
    if (u.u_rdir) {
        plock(u.u_rdir);
        iput(u.u_rdir);
    }
    xfree();
    acct();
    mfree(coremap, p->p_size, p->p_addr);
    // The image this process's u-area calls home has just ceased to exist, and the live
    // u-area at UBASE is still ours until swtch() below hands the machine over.  Say so, or
    // the resume() inside that swtch() flushes 1024 words of a dead process into core that
    // malloc() may already have given away.  See the invariant at xswap() in text.c.
    if (p->p_addr == uhome)
        uhome = NOUHOME;
    p->p_stat                     = SZOMB;
    ((struct xproc *)p)->xp_xstat = rv;
    ((struct xproc *)p)->xp_utime = u.u_cutime + u.u_utime;
    ((struct xproc *)p)->xp_stime = u.u_cstime + u.u_stime;
    for (q = &proc[0]; q < &proc[NPROC]; q++)
        if (q->p_ppid == p->p_pid) {
            wakeup((chan_t)&proc[1]);
            q->p_ppid = 1;
            if (q->p_stat == SSTOP)
                setrun(q);
        }
    for (q = &proc[0]; q < &proc[NPROC]; q++)
        if (p->p_ppid == q->p_pid) {
            wakeup((chan_t)q);
            swtch();
            // no return
        }
    swtch();
}

// Wait system call.
// Search for a terminated (zombie) child,
// finally lay it to rest, and collect its status.
// Look also for stopped (traced) children,
// and pass back status from them.
void wait()
{
    register int f;
    register struct proc *p;

    f = 0;

loop:
    for (p = &proc[0]; p < &proc[NPROC]; p++)
        if (p->p_ppid == u.u_procp->p_pid) {
            f++;
            if (p->p_stat == SZOMB) {
                u.u_r.r_val1 = p->p_pid;
                u.u_r.r_val2 = ((struct xproc *)p)->xp_xstat;
                u.u_cutime += ((struct xproc *)p)->xp_utime;
                u.u_cstime += ((struct xproc *)p)->xp_stime;
                p->p_pid   = 0;
                p->p_ppid  = 0;
                p->p_pgrp  = 0;
                p->p_sig   = 0;
                p->p_flag  = 0;
                p->p_wchan = 0;
                p->p_stat  = NULL;
                return;
            }
            if (p->p_stat == SSTOP) {
                if ((p->p_flag & SWTED) == 0) {
                    p->p_flag |= SWTED;
                    u.u_r.r_val1 = p->p_pid;
                    u.u_r.r_val2 = (fsig(p) << 8) | 0177;
                    return;
                }
                continue;
            }
        }
    if (f) {
        sleep((chan_t)u.u_procp, PWAIT);
        goto loop;
    }
    u.u_error = ECHILD;
}

// fork system call.
void fork()
{
    register struct proc *p1, *p2;
    register int a;

    // Make sure there's enough swap space for max
    // core image, thus reducing chances of running out
    if ((a = malloc(swapmap, wtodb(MAXMEM))) == 0) {
        u.u_error = ENOMEM;
        return;
    }
    mfree(swapmap, wtodb(MAXMEM), a);
    a  = 0;
    p2 = NULL;
    for (p1 = &proc[0]; p1 < &proc[NPROC]; p1++) {
        if (p1->p_stat == NULL && p2 == NULL)
            p2 = p1;
        else {
            if (p1->p_uid == u.u_uid && p1->p_stat != NULL)
                a++;
        }
    }
    // Disallow if
    //  No processes at all;
    //  not su and too many procs owned; or
    //  not su and would take last slot.
    if (p2 == NULL || (u.u_uid != 0 && (p2 == &proc[NPROC - 1] || a > MAXUPRC))) {
        u.u_error = EAGAIN;
        return;
    }
    // Parent and child are told apart by the SECOND return value, r12 (R_VAL2 in
    // reg.h), which is v7's own answer: 1 in the child, 0 in the parent.  Nothing here
    // advances the saved PC to tell the two apart: the extracode gate already stores
    // nextpc in ERET (kernel/syscall.c), so there is nothing left to skip -- and RET is
    // a WORD address on this machine, so bumping it would step whole instruction words.
    //
    // r_val1 alone cannot do it: each side gets the OTHER's pid, which is distinct
    // but not self-identifying.
    p1 = u.u_procp;
    if (newproc()) {
        u.u_r.r_val1 = p1->p_pid; // the child: parent's pid ...
        u.u_r.r_val2 = 1;         // ... and "you are the child"
        u.u_start    = time;
        u.u_cstime   = 0;
        u.u_stime    = 0;
        u.u_cutime   = 0;
        u.u_utime    = 0;
        u.u_acflag   = AFORK;
        return;
    }
    u.u_r.r_val1 = p2->p_pid; // the parent: child's pid ...
    u.u_r.r_val2 = 0;         // ... and "you are the parent"
}

// break system call.
//  -- bad planning: "break" is a dirty word in C.
void sbreak()
{
    struct a {
        char *nsiz;
    };
    register int a, n, d;
    int i;

    // set n to new data size
    // set d to new-old
    // set n to new total size

    // The argument is a virtual WORD address, not a byte count: on a word machine
    // the break names a word, and cmd/sim/syscall.cpp already read it that way.  v7's
    // btow() here made the two gates disagree by a factor of six, so it is gone --
    // libc's sbrk() converts its byte increment with btow() on its own side and hands
    // the gate a whole number of words.
    //
    // ptrword() masks to bits 15-1, which is cmd/sim's `& BITS(15)' spelled in C: a
    // caller may pass either a fat char * (curbrk) or a plain word address, and the
    // two implementations of the gate cannot drift.  The byte offset is dropped, so
    // the ABI is a word-ALIGNED pointer; a mid-word char * floors the break to its
    // word.  The mask also makes the value non-negative, so the `n < 0' below is now
    // purely about a break that lands below the text.
    n = pground(ptrword(((struct a *)u.u_ap)->nsiz));
    if (!u.u_sep)
        n -= u.u_tsize;
    if (n < 0)
        n = 0;
    d = n - u.u_dsize;
    n += USIZE + u.u_ssize;
    // The break stops at virtual page USTKPAGE (070000), where the stack begins: that is
    // estabur()'s `nt + nd > USTKPAGE * PGSZ' check (utab.c), which is why there is no
    // ceiling spelled out here.  And estabur() assigns u_dsize itself, so there is no
    // trailing `u.u_dsize += d', which would count the change twice.  `d' below is still
    // the delta the copyseg shuffle needs: data grows in the MIDDLE of the image, so unlike
    // the stack's, its growth really does move the pages above it.
    if (estabur(u.u_tsize, u.u_dsize + d, u.u_ssize, u.u_sep, RO))
        return;
    if (d > 0)
        goto bigger;
    a = u.u_procp->p_addr + n - u.u_ssize;
    i = n;
    for (n = u.u_ssize; n > 0; n -= PGSZ) {
        copyseg(a - d, a);
        a += PGSZ;
    }
    expand(i);
    return;

bigger:
    expand(n);
    a = u.u_procp->p_addr + n;
    for (n = u.u_ssize; n > 0; n -= PGSZ) {
        a -= PGSZ;
        copyseg(a - d, a);
    }
    for (; d > 0; d -= PGSZ) {
        a -= PGSZ;
        clearseg(a);
    }
}
