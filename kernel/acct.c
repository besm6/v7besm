/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/acct.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/inode.h"
#include "sys/proc.h"
#include "sys/seg.h"
// clang-format on

/*
 * Perform process accounting functions.
 */
void sysacct()
{
    register struct inode *ip;
    register struct a {
        char *fname;
    } *uap;

    uap = (struct a *)u.u_ap;
    if (suser()) {
        if (uap->fname == NULL) {
            if (acctp) {
                plock(acctp);
                iput(acctp);
                acctp = NULL;
            }
            return;
        }
        if (acctp) {
            u.u_error = EBUSY;
            return;
        }
        ip = namei(uchar, 0);
        if (ip == NULL)
            return;
        if ((ip->i_mode & IFMT) != IFREG) {
            u.u_error = EACCES;
            iput(ip);
            return;
        }
        acctp = ip;
        prele(ip);
    }
}

/*
 * On exit, write a record on the accounting file.
 */
void acct()
{
    register int i;
    register struct inode *ip;
    off_t siz;

    if ((ip = acctp) == NULL)
        return;
    plock(ip);
    for (i = 0; i < sizeof(acctbuf.ac_comm); i++)
        acctbuf.ac_comm[i] = u.u_comm[i];
    acctbuf.ac_utime = compress(u.u_utime);
    acctbuf.ac_stime = compress(u.u_stime);
    acctbuf.ac_etime = compress(time - u.u_start);
    acctbuf.ac_btime = u.u_start;
    acctbuf.ac_uid   = u.u_ruid;
    acctbuf.ac_gid   = u.u_rgid;
    acctbuf.ac_mem   = 0;
    acctbuf.ac_io    = 0;
    acctbuf.ac_tty   = u.u_ttyd;
    acctbuf.ac_flag  = u.u_acflag;
    siz              = ip->i_size;
    u.u_offset       = siz;
    u.u_base         = (caddr_t)&acctbuf;
    u.u_count        = sizeof(acctbuf);
    u.u_segflg       = 1;
    u.u_error        = 0;
    writei(ip);
    if (u.u_error)
        ip->i_size = siz;
    prele(ip);
}

/*
 * Produce a pseudo-floating point representation
 * with 3 bits base-8 exponent, 13 bits fraction.
 */
int compress(register time_t t)
{
    register int exp = 0, round = 0;

    while (t >= 8192) {
        exp++;
        round = t & 04;
        t >>= 3;
    }
    if (round) {
        t++;
        if (t >= 8192) {
            t >>= 3;
            exp++;
        }
    }
    return ((exp << 13) + t);
}

/*
 * lock user into core as much
 * as possible. swapping may still
 * occur if core grows.
 */
void syslock()
{
    register struct proc *p;
    register struct a {
        int flag;
    } *uap;

    uap = (struct a *)u.u_ap;
    if (suser()) {
        p = u.u_procp;
        p->p_flag &= ~SULOCK;
        if (uap->flag)
            p->p_flag |= SULOCK;
    }
}
