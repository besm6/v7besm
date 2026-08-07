// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The user structure.
// One allocated per process.
// Contains all per process data
// that doesn't need to be referenced
// while the process is swapped.
// The user block is one page, USIZE
// words, at the physical address
// UBASE (074000); contains the system
// stack per user; is cross referenced
// with the proc structure for the
// same process.  A context switch
// copies it as far as the stack is
// live -- u_stkdepth words, never
// more than USIZE.  The kernel space
// ends one page higher: the stack may
// grow past USIZE into 076000-077777,
// which is NOT saved at all.
// See UBASE in <sys/param.h>.

#ifndef _SYS_USER_H
#define _SYS_USER_H

// Included, not assumed of the caller; see sys/dir.h.  That one is the hard case in this
// directory: u_dent below holds a `struct direct' BY VALUE, so a forward declaration of the
// tag will not do -- this header genuinely cannot compile without sys/dir.h ahead of it, and
// sys/dir.h sorts first anyway.  param.h and types.h come with it, and are named regardless:
// a header should say what it uses, and sys/dir.h is not in the business of promising them.
#include <sys/dir.h>    // struct direct
#include <sys/errno.h>  // the u_error codes -- one home, and this is not it
#include <sys/param.h>  // DIRSIZ, NOFILE
#include <sys/signal.h> // NSIG
#include <sys/types.h>  // label_t, off_t, time_t, caddr_t, dev_t

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
    unsigned u_upt[8];    // The shadow page table
    // How many words of this u-area the last uflush() copied: struct user plus the
    // live kernel stack (everything below r15), rounded up by a margin.  Written by
    // uflush() into the SAVED copy and read back by uload() before it copies, so the
    // count travels inside the page it describes -- see kernel/uarea.S.  In the live
    // copy it is whatever uload() brought in, i.e. the depth this process was loaded
    // with, not its depth now.  Its word offset is hardcoded in the assembly as
    // USTKD = UPT + 8, so it must stay immediately after u_upt[].
    int u_stkdepth;
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
    char u_intflg;      // catch intr from sys
    char u_justreturn;  // this call restored the frame itself; see sigret(), sendsig.c
    char u_sep;         // flag for I and D separation
    struct tty *u_ttyp; // controlling tty pointer
    dev_t u_ttyd;       // controlling tty dev
    struct {              // header of executable file (cross/besm6/b.out.h)
        unsigned ux_mag;  // a_magic  -- MUST be unsigned: FMAGIC is 48 bits, an int is 41
        int ux_csize;     // a_const  const segment size, bytes
        int ux_tsize;     // a_text   text  segment size, bytes
        int ux_dsize;     // a_data   data  segment size, bytes
        int ux_bsize;     // a_bss    bss   size, bytes
        int ux_ssize;     // a_syms   symbol table size, bytes (unused by exec)
        int ux_entloc;    // a_entry  entry point, a WORD address
        int ux_relflg;    // a_flag
    } u_exdata;
    char u_comm[DIRSIZ];
    time_t u_start;
    char u_acflag;
    int u_fpflag; // unused now, will be later
    int u_cmask;  // mask for file creation
    int u_stack[1];
    // kernel stack per user
    // grows up from here; saved as far
    // as r15 has reached (u_stkdepth,
    // at most u + USIZE = 075777), then
    // overflow to 0100000 -- see above
};

extern struct user u;

#endif // _SYS_USER_H
