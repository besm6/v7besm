/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 2007 Robert Nordier. All rights reserved. */

//
// pstat -- print system facts.
//
//      pstat [ -acfimpstx ] [ -u addr ]
//
// Task C8's fifth and widest.  It is the heaviest reader of kernel/kctl.c -- eleven of the
// table's rows name this program -- and it is a rewrite rather than a port, for the reason
// ../TODO.md gives: v7's pstat is 385 lines tied to PDP-11 structures, and the parts of it
// that are not the report are the parts that do not survive.
//
// EVERY TABLE COMES THROUGH kctl(2), WHICH IS UNPRIVILEGED, and that is a correction to what
// ../TODO.md says.  The brief lumps this program in with ps as "root and only root"; that is
// true of ps, whose u-area has to come off a memory device, and it is true HERE ONLY OF -u.
// The inode, file, text, proc, mount and terminal tables and the two allocation maps are all
// KCTL_GET, so seven of the eight modes work for anybody.  -u opens /dev/kmem and /dev/mem,
// both mode 0640 and root's, and is the one that fails for everybody else.
//
// WHAT `LOC' IS NOW.  v7 printed a 24-bit byte address masked out of a PDP-11 kernel
// pointer.  Here it is the WORD address of the table entry, printed octal, and it is
// computed the way lib/test/kctlt.c computes it: kgetsym(name) is the base and the stride is
// sizeof(struct x)/NBPW.  NEVER WRITE A STRIDE AS A LITERAL -- cmd/sim/kernel.h carries a
// measured copy of every one of these layouts, and the whole point of computing it here from
// the real header is that a divergence between the two shows up as a failing test.  Because
// every LOC, every IPTR, every INODP and every TEXTP is a word address in the same space,
// the tables cross-reference by eye with no index column added.
//
// dotty() IS WRITTEN FROM SCRATCH.  v7's prints `1 kl11', reads _kl11, then walks _ndh11 and
// _dh11[48] -- three symbols that are PDP-11 communications hardware and have no counterpart
// anywhere.  Here there is one terminal driver and one array, sc[NSC], the two Consul
// typewriters (kernel/dev/sc.c).  Two columns changed with it: v7's ADDR was the UNIBUS
// address of the line's registers and is called LINE here, <sys/tty.h> saying outright that
// t_addr is a device/line number and not an address on this machine; and the queue counts
// come out of this port's own struct clist, whose c_cc is the one field of it that is v7's.
//
// TWO MODES ARE NEW, AND THEY ARE NOT DECORATION.  kernel/kctl.c's discipline is that a row
// names the program that asked for it and that a row whose column is empty does not belong.
// Six rows -- mount, coremap, swapmap, nswap, swplo, swapdev -- name pstat, and v7's pstat
// prints not one of them; two more -- time and lbolt -- named ps, which has no column for
// either.  So:
//
//   -m  the mount table.  One line per mounted filesystem.
//   -s  the paging store and the two allocation maps.  coremap hands out WORDS and swapmap
//       hands out BLOCKS of BSIZE (<sys/map.h>); the two are printed under separate headings
//       with their units named, because one table of both would be exactly the kind of
//       silent unit change ../README.md SS4 is about.
//   -c  the system clock, to sub-second resolution: `time' is seconds since the epoch and
//       `lbolt' is ticks into the current second (kernel/clock.c).  time(2) returns whole
//       seconds, so this is the only way userland can see the fraction at all.  It is what
//       gives those two rows a reader; they are re-commented `pstat' in the table.
//
// THREE v7 PRINTF BUGS ARE FIXED RATHER THAN CARRIED, and they are all one bug wearing three
// hats (../README.md SS3): `%6l' for i_number is a length modifier with no conversion after
// it, and `%.1o' and `%8.1o' are a precision of 1 on an octal, which was v7's idiom for "at
// least one digit" and is not what it prints.  Every number here is %d or %o with a width.
//
// NOT SETUID, AND IT MUST NOT BECOME ONE, for -u's sake: /dev/mem is every process's memory,
// and a setuid pstat would hand it out through a program that already knows the layout.
//
// <unistd.h> IS DELIBERATELY NOT INCLUDED, AND THIS IS THE ONLY SOURCE IN THE TREE THAT
// CANNOT HAVE IT.  <sys/mount.h> declares the kernel's table -- `extern struct mount
// mount[NMOUNT]' -- and <unistd.h> declares the system call, `int mount(const char *, const
// char *, int)'.  The two names are the same name, and b6parse refuses the second one it
// sees: "Variable mount redeclared with different type".  Nothing else hits it because
// nothing else has ever wanted the mount table from user space; the mount(1) command wants
// the call and not the table, and the kernel wants the table and links no libc.
//
// So the three calls this file needs are declared here, and only these three.  They are
// copied from <unistd.h> and must stay identical to it.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/kctl.h>
#include <sys/map.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/text.h>
#include <sys/tty.h>
#include <sys/types.h>
#include <sys/user.h>

ssize_t read(int fd, void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);

// EVERY TABLE IS BSS AND NOT A FRAME.  v7 declared each of these as an automatic inside the
// routine that prints it, and struct proc xproc[NPROC] alone is 1,800 words of the 4,096 the
// user stack has -- the third ceiling of ../README.md SS6, which nothing checks.  Together
// these come to about 2,980 words, a tenth of the image ceiling and none of the stack.
static struct inode xinode[NINODE];
static struct text xtext[NTEXT];
static struct proc xproc[NPROC];
static struct file xfile[NFILE];
static struct mount xmount[NMOUNT];
static struct tty xtty[NSC];
static struct map xcore[CMAPSIZ];
static struct map xswap[SMAPSIZ];
static struct user xu;

static int allflg;
static int inof, txtf, prcf, ttyf, usrf, filf, mntf, swpf, clkf;
static int ubase;

// Word strides, computed from the real headers.  See the file header on why not literals.
#define WORDS(t) ((int)(sizeof(t) / NBPW))

static void putf(int v, int c)
{
    putchar(v ? c : ' ');
}

//
// The base word address of an exported table, with one diagnostic for the whole program.
//
static int base(const char *name)
{
    int a = kgetsym(name);

    if (a < 0)
        fprintf(stderr, "pstat: this kernel exports no %s\n", name);
    return a;
}

//
// A whole table.  A short answer means the kernel and this program disagree about a size,
// which is a table worth refusing rather than half printing.
//
static int table(const char *name, void *buf, int len)
{
    if (kctl(name, KCTL_GET, buf, len) != len) {
        fprintf(stderr, "pstat: cannot read %s\n", name);
        return -1;
    }
    return 0;
}

static void doinode(void)
{
    struct inode *ip;
    int loc, nin = 0;

    if ((loc = base("inode")) < 0 || table("inode", xinode, sizeof xinode) < 0)
        return;
    for (ip = xinode; ip < &xinode[NINODE]; ip++)
        if (ip->i_count)
            nin++;
    printf("%d active inodes\n", nin);
    printf("     LOC  FLAGS CNT DEVICE   INO   MODE NLK UID  SIZE/DEV\n");
    for (ip = xinode; ip < &xinode[NINODE]; ip++, loc += WORDS(struct inode)) {
        if (ip->i_count == 0)
            continue;
        printf("%8o ", loc);
        putf(ip->i_flag & ILOCK, 'L');
        putf(ip->i_flag & IUPD, 'U');
        putf(ip->i_flag & IACC, 'A');
        putf(ip->i_flag & IMOUNT, 'M');
        putf(ip->i_flag & IWANT, 'W');
        putf(ip->i_flag & ITEXT, 'T');
        printf("%4d", ip->i_count & 0377);
        printf("%3d,%3d", major(ip->i_dev), minor(ip->i_dev));
        printf("%6d", ip->i_number);
        printf("%7o", ip->i_mode);
        printf("%4d", ip->i_nlink);
        printf("%4d", ip->i_uid);
        if ((ip->i_mode & IFMT) == IFBLK || (ip->i_mode & IFMT) == IFCHR)
            printf("%6d,%3d", major((dev_t)ip->i_un.i_rdev), minor((dev_t)ip->i_un.i_rdev));
        else
            printf("%10d", ip->i_size);
        printf("\n");
    }
}

static void dotext(void)
{
    struct text *xp;
    int loc, ntx = 0;

    if ((loc = base("text")) < 0 || table("text", xtext, sizeof xtext) < 0)
        return;
    for (xp = xtext; xp < &xtext[NTEXT]; xp++)
        if (xp->x_iptr != NULL)
            ntx++;
    printf("%d text segments\n", ntx);
    printf("     LOC  FLAGS DADDR   CADDR    SIZE     IPTR CNT CCNT\n");
    for (xp = xtext; xp < &xtext[NTEXT]; xp++, loc += WORDS(struct text)) {
        if (xp->x_iptr == NULL)
            continue;
        printf("%8o ", loc);
        putf(xp->x_flag & XTRC, 'T');
        putf(xp->x_flag & XWRIT, 'W');
        putf(xp->x_flag & XLOAD, 'L');
        putf(xp->x_flag & XLOCK, 'K');
        putf(xp->x_flag & XWANT, 'w');
        printf("%6d", xp->x_daddr);
        printf("%8o", xp->x_caddr);
        printf("%8d", xp->x_size);
        printf("%9o", ptrword(xp->x_iptr));
        printf("%4d", xp->x_count & 0377);
        printf("%5d", xp->x_ccount & 0377);
        printf("\n");
    }
}

static void doproc(void)
{
    struct proc *pp;
    int loc, np = 0;

    if ((loc = base("proc")) < 0 || table("proc", xproc, sizeof xproc) < 0)
        return;
    for (pp = xproc; pp < &xproc[NPROC]; pp++)
        if (pp->p_stat)
            np++;
    printf("%d processes\n", np);
    printf("     LOC S  F  PRI SIGNAL  UID TIM CPU NI  PGRP   PID  PPID"
           "    ADDR   SIZE   WCHAN    LINK   TEXTP CLKT\n");
    for (pp = xproc; pp < &xproc[NPROC]; pp++, loc += WORDS(struct proc)) {
        if (pp->p_stat == 0 && allflg == 0)
            continue;
        printf("%8o", loc);
        printf("%2d", pp->p_stat);
        printf("%3o", pp->p_flag);
        printf("%5d", pp->p_pri);
        printf("%7o", pp->p_sig);
        printf("%5d", pp->p_uid);
        printf("%4d", pp->p_time & 0377);
        printf("%4d", pp->p_cpu & 0377);
        printf("%3d", pp->p_nice);
        printf("%6d", pp->p_pgrp);
        printf("%6d", pp->p_pid);
        printf("%6d", pp->p_ppid);
        printf("%8o", (int)pp->p_addr);
        printf("%7d", pp->p_size);
        printf("%8o", (int)pp->p_wchan);
        printf("%8o", ptrword(pp->p_link));
        printf("%8o", ptrword(pp->p_textp));
        printf("%5d", pp->p_clktim);
        printf("\n");
    }
}

static void ttyprt(int n, struct tty *tp)
{
    printf("%2d", n);
    printf("%4d", tp->t_rawq.c_cc);
    printf("%4d", tp->t_canq.c_cc);
    printf("%4d", tp->t_outq.c_cc);
    printf("%8o", tp->t_flags);
    printf("%6d", tp->t_addr);
    printf("%4d", tp->t_delct);
    printf("%4d ", tp->t_col);
    putf(tp->t_state & TIMEOUT, 'T');
    putf(tp->t_state & WOPEN, 'W');
    putf(tp->t_state & ISOPEN, 'O');
    putf(tp->t_state & CARR_ON, 'C');
    putf(tp->t_state & BUSY, 'B');
    putf(tp->t_state & ASLEEP, 'A');
    putf(tp->t_state & XCLUDE, 'X');
    putf(tp->t_state & HUPCLS, 'H');
    printf("%6d", tp->t_pgrp);
    printf("\n");
}

static void dotty(void)
{
    int i;

    if (table("sc", xtty, sizeof xtty) < 0)
        return;
    printf("%d Consul lines\n", NSC);
    printf(" # RAW CAN OUT   FLAGS  LINE DEL COL STATE     PGRP\n");
    for (i = 0; i < NSC; i++)
        ttyprt(i, &xtty[i]);
}

static void dofil(void)
{
    struct file *fp;
    int loc, nf = 0;

    if ((loc = base("file")) < 0 || table("file", xfile, sizeof xfile) < 0)
        return;
    for (fp = xfile; fp < &xfile[NFILE]; fp++)
        if (fp->f_count)
            nf++;
    printf("%d open files\n", nf);
    printf("     LOC FLG CNT      INO    OFFS\n");
    for (fp = xfile; fp < &xfile[NFILE]; fp++, loc += WORDS(struct file)) {
        if (fp->f_count == 0)
            continue;
        printf("%8o ", loc);
        putf(fp->f_flag & FREAD, 'R');
        putf(fp->f_flag & FWRITE, 'W');
        putf(fp->f_flag & FPIPE, 'P');
        printf("%4d", fp->f_count & 0377);
        printf("%9o", ptrword(fp->f_inode));
        printf("%8d\n", fp->f_un.f_offset);
    }
}

//
// -m.  The mount table, which v7's pstat never printed and which kernel/kctl.c exports on
// this program's account.  Slot 0 is the root and is always in use (kernel/main.c).
//
static void domount(void)
{
    struct mount *mp;
    int loc, nm = 0;

    if ((loc = base("mount")) < 0 || table("mount", xmount, sizeof xmount) < 0)
        return;
    for (mp = xmount; mp < &xmount[NMOUNT]; mp++)
        if (mp->m_bufp != NULL)
            nm++;
    printf("%d mounted file systems\n", nm);
    printf("     LOC  DEVICE     BUFP    INODP\n");
    for (mp = xmount; mp < &xmount[NMOUNT]; mp++, loc += WORDS(struct mount)) {
        if (mp->m_bufp == NULL && allflg == 0)
            continue;
        printf("%8o", loc);
        printf("%4d,%3d", major(mp->m_dev), minor(mp->m_dev));
        printf("%9o", ptrword(mp->m_bufp));
        printf("%9o", ptrword(mp->m_inodp));
        printf("\n");
    }
}

//
// One allocation map.  THE UNITS DIFFER BETWEEN THE TWO and are named by the caller:
// coremap hands out words of physical core, swapmap blocks of BSIZE (<sys/map.h>).
//
static void prmap(const char *name, const char *unit, struct map *mp, int n, int loc)
{
    // `avail' and not `free': a local of that name shadows <stdlib.h>'s free() and this
    // compiler refuses the redeclaration outright rather than shadowing it.
    int i, avail = 0, used = 0;

    for (i = 0; i < n; i++)
        if (mp[i].m_size) {
            avail += mp[i].m_size;
            used++;
        }
    printf("%s: %d fragments, %d %s free\n", name, used, avail, unit);
    printf("     LOC     SIZE     ADDR\n");
    for (i = 0; i < n; i++, loc += WORDS(struct map)) {
        if (mp[i].m_size == 0)
            continue;
        printf("%8o%9d%9d\n", loc, mp[i].m_size, mp[i].m_addr);
    }
}

//
// -s.  The paging store and the two maps.
//
static void doswap(void)
{
    int nswap = 0, swplo = 0, cloc, sloc;
    dev_t swapdev = 0;

    if (table("nswap", &nswap, sizeof nswap) < 0 ||
        table("swplo", &swplo, sizeof swplo) < 0 ||
        table("swapdev", &swapdev, sizeof swapdev) < 0)
        return;
    printf("swapdev %d,%d  swplo %d  nswap %d blocks of %d bytes\n", major(swapdev),
           minor(swapdev), swplo, nswap, BSIZE);

    if ((cloc = base("coremap")) >= 0 && table("coremap", xcore, sizeof xcore) == 0)
        prmap("coremap", "words", xcore, CMAPSIZ, cloc);
    if ((sloc = base("swapmap")) >= 0 && table("swapmap", xswap, sizeof xswap) == 0)
        prmap("swapmap", "blocks", xswap, SMAPSIZ, sloc);
}

//
// -c.  The system clock, to sub-second resolution.  time(2) hands out whole seconds; the
// kernel keeps the fraction in lbolt and this is the only way to see it.
//
static void doclock(void)
{
    time_t t = 0;
    int lbolt = 0;

    if (table("time", &t, sizeof t) < 0 || table("lbolt", &lbolt, sizeof lbolt) < 0)
        return;
    printf("time %d  lbolt %d/%d second\n", (int)t, lbolt, HZ);
    printf("%s", ctime(&t));
}

//
// -u.  The one mode that opens a device, and so the one that is root's.
//
// THE ADDRESS IS ps -l's ADDR COLUMN, with no shift: v7's `lseek(fc, ubase<<12, 0)' was a
// PDP-11 click-to-byte conversion of a click address, and ADDR is a word address here.  The
// three cases are ps's own and ../ps/ps.c is the account of them -- the live u-area at UBASE
// wins for the process that owns it, whatever its image says.  0 names the live one outright.
//
static void dousr(void)
{
    struct user *up = &xu;
    int uhome = 0, fd, i;

    if (table("uhome", &uhome, sizeof uhome) < 0)
        return;

    if (ubase == 0 || ubase == uhome) {
        fd = open("/dev/kmem", O_RDONLY);
        i  = UBASE;
    } else {
        fd = open("/dev/mem", O_RDONLY);
        i  = ubase;
    }
    if (fd < 0) {
        fputs("pstat: no memory device (-u is for the super-user)\n", stderr);
        return;
    }
    if (lseek(fd, (off_t)i * NBPW, 0) < 0 || read(fd, up, sizeof xu) != (int)sizeof xu) {
        fprintf(stderr, "pstat: cannot read the u-area at %o\n", i);
        close(fd);
        return;
    }
    close(fd);

    printf("u-area at %o%s\n", i, i == UBASE ? " (the live one)" : "");
    printf("segflg, error %d, %d\n", up->u_segflg, up->u_error);
    printf("uids %d,%d,%d,%d\n", up->u_uid, up->u_gid, up->u_ruid, up->u_rgid);
    printf("procp %o\n", ptrword(up->u_procp));
    printf("base, count, offset %o %d %d\n", ptrword(up->u_base), up->u_count, up->u_offset);
    printf("cdir %o  rdir %o  pdir %o\n", ptrword(up->u_cdir), ptrword(up->u_rdir),
           ptrword(up->u_pdir));
    // %.*s with DIRSIZ, not v7's %.14s: a name out of a directory is DIRSIZ bytes and is not
    // NUL-terminated when it fills them (../README.md SS5).
    printf("dbuf %.*s\n", DIRSIZ, up->u_dbuf);
    printf("dent %d %.*s\n", up->u_dent.d_ino, DIRSIZ, up->u_dent.d_name);
    printf("file");
    for (i = 0; i < NOFILE; i++) {
        if (i && i % 10 == 0)
            printf("\n    ");
        printf("%8o", ptrword(up->u_ofile[i]));
    }
    printf("\nargs");
    for (i = 0; i < 5; i++)
        printf(" %o", up->u_arg[i]);
    // The shadow page table, which is where v7's `XXX print page tables?' comment was: РП
    // and РЗ cannot be read back, so u_upt[8] is the only copy of this process's mapping
    // (kernel/utab.c, doc/Memory_Mapping.md).
    printf("\nupt ");
    for (i = 0; i < 8; i++)
        printf(" %o", up->u_upt[i]);
    printf("\nstkdepth %d\n", up->u_stkdepth);
    printf("sizes %d %d %d words\n", up->u_tsize, up->u_dsize, up->u_ssize);
    printf("times %d %d\n", up->u_utime / HZ, up->u_stime / HZ);
    printf("ctimes %d %d\n", up->u_cutime / HZ, up->u_cstime / HZ);
    printf("ttyp %o\n", ptrword(up->u_ttyp));
    printf("ttydev %d,%d\n", major(up->u_ttyd), minor(up->u_ttyd));
    printf("comm %.*s\n", DIRSIZ, up->u_comm);
}

//
// v7's, and octal because ps -l's ADDR is octal.  A non-octal digit ends the number.
//
static int oatoi(const char *s)
{
    int v = 0;

    while (*s >= '0' && *s <= '7')
        v = (v << 3) + (*s++ - '0');
    return v;
}

static void usage(void)
{
    fputs("usage: pstat [ -acfimpstx ] [ -u addr ]\n", stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    while (--argc && **++argv == '-') {
        while (*++*argv)
            switch (**argv) {
            case 'a':
                allflg++;
                break;
            case 'i':
                inof++;
                break;
            case 'x':
                txtf++;
                break;
            case 'p':
                prcf++;
                break;
            case 't':
                ttyf++;
                break;
            case 'f':
                filf++;
                break;
            case 'm':
                mntf++;
                break;
            case 's':
                swpf++;
                break;
            case 'c':
                clkf++;
                break;
            case 'u':
                if (--argc == 0)
                    usage();
                usrf++;
                ubase = oatoi(*++argv);
                goto next;
            default:
                usage();
            }
    next:;
    }
    // v7 took a core file and a namelist as trailing arguments.  There is no namelist on this
    // system and every table comes through kctl(2), so there is nothing left for either.
    if (argc > 0)
        usage();

    if (inof)
        doinode();
    if (txtf)
        dotext();
    if (ttyf)
        dotty();
    if (prcf)
        doproc();
    if (filf)
        dofil();
    if (mntf)
        domount();
    if (swpf)
        doswap();
    if (clkf)
        doclock();
    if (usrf)
        dousr();
    return 0;
}
