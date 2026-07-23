// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The user structure.
// One allocated per process.
// Contains all per process data
// that doesn't need to be referenced
// while the process is swapped.
// The user block is one page, USIZE
// words, at the physical address
// UBASE (076000) -- the last page of
// the kernel space; contains the system
// stack per user; is cross referenced
// with the proc structure for the
// same process.

#ifndef _SYS_USER_H
#define _SYS_USER_H

#define EXCLOSE 01

struct user {
    label_t u_rsav;       // save info when exchanging stacks
    char u_segflg;        // IO flag: 0:user D; 1:system; 2:user I
    char u_error;         // return error code
    int u_uid;            // effective user id
    int u_gid;            // effective group id
    int u_ruid;           // real user id
    int u_rgid;           // real group id
    struct proc *u_procp; // pointer to proc structure
    int *u_ap;            // pointer to arglist
    union {               // syscall return values
        struct {
            int val1;
            int val2;
        } u_r0;
        off_t r_off;
        time_t r_time;
    } u_r;
#define r_val1 u_r0.val1
#define r_val2 u_r0.val2
    caddr_t u_base;       // base address for IO
    int u_count;          // bytes remaining for IO
    off_t u_offset;       // offset in file for IO
    struct inode *u_cdir; // pointer to inode of current directory
    struct inode *u_rdir; // root directory of current process
    char u_dbuf[DIRSIZ];  // current pathname component
    caddr_t u_dirp;       // pathname pointer
    struct direct u_dent; // current directory entry
    struct inode *u_pdir; // inode of parent directory of dirp
    // The shadow page table, ready to load: eight words, each carrying a quartet
    // of РП descriptors for virtual pages 4i..4i+3 (accumulator bits 1-20 and
    // 29-48) and, in the even words, the matching РЗ protection byte for pages
    // 8j..8j+7 (bits 21-28, which РП leaves alone).  РП and РЗ cannot be read
    // back, so this is the only copy of the mapping.  See sureg() in utab.c and
    // doc/Memory_Mapping.md, "Programming the MMU".
    unsigned u_upt[8];
    struct file *u_ofile[NOFILE]; // pointers to file structures of open files
    char u_pofile[NOFILE];        // per-process flags of open files
    int u_arg[5];                 // arguments to current system call
    int u_tsize;                  // text size (words, a multiple of PGSZ)
    int u_dsize;                  // data size (words, a multiple of PGSZ)
    int u_ssize;                  // stack size (words, a multiple of PGSZ)
    label_t u_qsav;               // label variable for quits and interrupts
    label_t u_ssav;               // label variable for swapping
    int u_signal[NSIG];           // disposition of signals
    time_t u_utime;               // this process user time
    time_t u_stime;               // this process system time
    time_t u_cutime;              // sum of childs' utimes
    time_t u_cstime;              // sum of childs' stimes
    int *u_ar0;                   // address of users saved R0
    struct {                      // profile arguments
        int *pr_base;             // buffer base
        int pr_size;              // buffer size
        int pr_off;               // pc offset
        int pr_scale;             // pc scaling
    } u_prof;
    char u_intflg;      // catch intr from sys
    char u_justreturn;  // this call restored the frame itself; see sigret(), sendsig.c
    char u_sep;         // flag for I and D separation
    struct tty *u_ttyp; // controlling tty pointer
    dev_t u_ttyd;       // controlling tty dev
    struct {            // header of executable file
        int ux_mag;     // magic number
        int ux_tsize;   // text size
        int ux_dsize;   // data size
        int ux_bsize;   // bss size
        int ux_ssize;   // symbol table size
        int ux_entloc;  // entry location
        int ux_unused;
        int ux_relflg;
    } u_exdata;
    char u_comm[DIRSIZ];
    time_t u_start;
    char u_acflag;
    int u_fpflag; // unused now, will be later
    int u_cmask;  // mask for file creation
    int u_stack[1];
    // kernel stack per user
    // grows up from here to
    // u + USIZE words (0100000)
};

extern struct user u;

// u_error codes.  The user-level copy of this list is <errno.h>, which names
// the third copy as well; all three must agree number for number.
#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define ENXIO   6
#define E2BIG   7
#define ENOEXEC 8
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define ENOTBLK 15
#define EBUSY   16
#define EEXIST  17
#define EXDEV   18
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define ENOTTY  25
#define ETXTBSY 26
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define EMLINK  31
#define EPIPE   32
#define EDOM    33
#define ERANGE  34

#endif // _SYS_USER_H
