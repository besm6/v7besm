// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// One structure allocated per active
// process. It contains all data needed
// about the process while the
// process may be swapped out.
// Other per process data (user.h)
// is swapped with the process.
struct proc {
    char p_stat;
    char p_flag;
    char p_pri;           // priority, negative is high
    char p_time;          // resident time for scheduling
    char p_cpu;           // cpu usage for scheduling
    char p_nice;          // nice for cpu usage
    int p_sig;            // signals pending to this process
    int p_uid;            // user id, used to direct tty signals
    int p_pgrp;           // name of process group leader
    int p_pid;            // unique process id
    int p_ppid;           // process id of parent
    paddr_t p_addr;       // physical address of swappable image (word, page-aligned)
    int p_size;           // size of swappable image (words, a multiple of PGSZ)
    chan_t p_wchan;       // event process is awaiting
    struct text *p_textp; // pointer to text structure
    struct proc *p_link;  // linked list of running processes
    int p_clktim;         // time to alarm clock signal
};

extern struct proc proc[]; // the proc table itself

// stat codes
#define SSLEEP 1 // awaiting an event
#define SWAIT  2 // (abandoned state)
#define SRUN   3 // running
#define SIDL   4 // intermediate state in process creation
#define SZOMB  5 // intermediate state in process termination
#define SSTOP  6 // process being traced

// flag codes
#define SLOAD  01   // in core
#define SSYS   02   // scheduling process
#define SLOCK  04   // process cannot be swapped
#define SSWAP  010  // process is being swapped out
#define STRC   020  // process is being traced
#define SWTED  040  // another tracing flag
#define SULOCK 0100 // user settable lock in core

// parallel proc structure
// to replace part with times
// to be passed to parent process
// in ZOMBIE state.
struct xproc {
    char xp_stat;
    char xp_flag;
    char xp_pri;     // priority, negative is high
    char xp_time;    // resident time for scheduling
    char xp_cpu;     // cpu usage for scheduling
    char xp_nice;    // nice for cpu usage
    int xp_sig;      // signals pending to this process
    int xp_uid;      // user id, used to direct tty signals
    int xp_pgrp;     // name of process group leader
    int xp_pid;      // unique process id
    int xp_ppid;     // process id of parent
    int xp_xstat;    // Exit status for wait
    time_t xp_utime; // user time, this proc
    time_t xp_stime; // system time, this proc
};
