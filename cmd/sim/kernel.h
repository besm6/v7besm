//
// The imitation kernel: what b6sim shows a guest that asks about kernel state.
//
// b6sim runs ONE process and has no operating system inside it, so there is nothing behind
// kctl(2) or /dev/kmem to read.  This class is the pretence: a block of memory laid out the
// way the kernel's low core is laid out, holding the nineteen variables kernel/ksym.c
// exports and a `struct user' at UBASE, answered through the same three operations the real
// call offers and readable through the same two device nodes.
//
// WHY BOTHER, when the honest answer was ENOENT and used to be what this returned.  Because
// lib/test/kctlt was IMAGEONLY while it was, and an IMAGEONLY test runs only under libtest,
// which boots the kernel and is labelled `weekly' -- outside both daily gates.  With this
// here the same program runs in BOTH worlds against ONE .expected, which is the arrangement
// lib/test exists for and the only thing that can catch the two copies drifting apart.
//
// AND THAT IS ALSO THE GUARD ON THIS FILE.  The word offsets below are a second copy of
// layouts that live in <sys/proc.h> and <sys/user.h> -- headers a host C++ program cannot
// include, since they carry the kernel's dev_t and daddr_t.  Nothing here can check them.
// What checks them is kctlt, which computes the same numbers from the REAL headers and
// compares: `kc_size == NPROC * sizeof(struct proc)' and the u_procp arithmetic.  Get one of
// these wrong and that test fails under b6sim and passes on the image, which is exactly the
// signal the two-harness design is built to produce.  params.cpp covers the counts, which
// <sys/param.h> can be reached for.
//
// WHAT IS REAL AND WHAT IS NOT.  Every value b6sim genuinely knows is the real one -- the
// guest's own pid, uid and parent, the host clock, the bytes actually moved through the
// terminal, the instructions actually executed.  Everything b6sim has no counterpart for is
// LEFT ZERO rather than invented: there is no inode table here to describe, and a plausible
// fiction is worse than an empty one because a tool cannot tell it from a measurement.  The
// single exception is msgbuf, which carries a fixed banner so that a dmesg has one line to
// print; see BANNER below.
//
#ifndef DUBNA_KERNEL_H
#define DUBNA_KERNEL_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "besm6_arch.h"

//
// Constants respelled from include/sys/param.h.  params.cpp static_asserts every one of
// them against the kernel's own header, which it reaches by relative path; see there.
//
namespace kparam {
constexpr int NBPW    = 6;       // bytes per word
constexpr int KREACH  = 0100000; // what an unmapped access can name: /dev/kmem's ceiling
constexpr int KEND    = 054000;  // the kernel image must end below this
constexpr int UBASE   = 074000;  // the u-area
constexpr int HZ      = 250;     // ticks per second
constexpr int NPROC   = 150;
constexpr int NTEXT   = 40;
constexpr int NINODE  = 24;
constexpr int NFILE   = 50;
constexpr int NMOUNT  = 8;
constexpr int NSC     = 2;
constexpr int MSGBUFS = 128;
constexpr int CMAPSIZ = 50;
constexpr int SMAPSIZ = 50;
constexpr int SRUN    = 3;  // proc.h stat code
constexpr int SLOAD   = 01; // proc.h flag code
} // namespace kparam

//
// The kctl(2) interface, respelled from include/sys/kctl.h.  That header is #include-free
// and would compile here, but params.cpp checks these against it rather than syscall.cpp
// including it: one file in this library reaches into include/, and it is the one whose
// whole job is to prove the copies agree.
//
namespace kctlop {
constexpr int GET  = 0;
constexpr int SET  = 1; // reserved -- EINVAL, as in the kernel
constexpr int LIST = 2;
constexpr int STAT = 3;

constexpr int KSYMLEN    = 12; // width of one KCTL_LIST record, in bytes
constexpr int FLAG_RD    = 01; // KCTLF_RD
constexpr int STAT_WORDS = 3;  // struct kctlstat: kc_addr, kc_size, kc_flags
} // namespace kctlop

//
// Word offsets inside the guest structures this file fills in.  MEASURED, not derived --
// a guest program was compiled against the real headers and printed them, which is the
// habit doc/Besm6_Data_Representation.md asks for and the only way to be sure where a run
// of `char' members ends.  Named rather than counted, because these are the ABI between
// this file and lib/test/kctlt.
//
namespace klayout {
// struct proc -- <sys/proc.h>.  The six char members pack into word 0.
enum : int {
    P_CHARS  = 0, // p_stat p_flag p_pri p_time p_cpu p_nice, one byte each
    P_SIG    = 1,
    P_UID    = 2,
    P_PGRP   = 3,
    P_PID    = 4,
    P_PPID   = 5,
    P_ADDR   = 6,
    P_SIZE   = 7,
    P_WCHAN  = 8,
    P_TEXTP  = 9,
    P_LINK   = 10,
    P_CLKTIM = 11,
    P_WORDS  = 12,
};

// struct user -- <sys/user.h>.  Only the fields b6sim has an answer for are named; the
// rest of the 139 words stay zero.
enum : int {
    U_UID   = 11,
    U_GID   = 12,
    U_RUID  = 13,
    U_RGID  = 14,
    U_PROCP = 15,
    U_UTIME = 111,
    U_STIME = 112,
    U_TTYP  = 121,
    U_TTYD  = 122,
    U_COMM  = 131, // char[DIRSIZ]
    U_START = 134,
    U_WORDS = 139,
};

// The other exported aggregates, in words per element.
enum : int {
    TEXT_WORDS  = 5,
    INODE_WORDS = 17,
    FILE_WORDS  = 3,
    MOUNT_WORDS = 3,
    TTY_WORDS   = 29,
    MAP_WORDS   = 2,
    DKTIME_N    = 32, // dk_time[32]
};
} // namespace klayout

//
// The imitation kernel.  One per Machine.
//
class SimKernel {
public:
    // One row of the kctl(2) table, in the order kernel/ksym.c declares them.
    struct Entry {
        const char *name; // NUL-terminated; at most KSYMLEN-1 characters
        unsigned addr;    // word address inside the image
        unsigned size;    // bytes
    };

    SimKernel();

    // The table.  find() returns nullptr for a name that is not exported.
    const Entry *find(const std::string &name) const;
    int count() const { return (int)table.size(); }
    const Entry &entry(int i) const { return table[i]; }

    // Bring the live values up to date -- the clock, the tick counters and the terminal
    // counters.  Called before every answer, so a guest that reads twice sees time move.
    void refresh(uint64_t instructions);

    // Copy `n` bytes starting at word `addr` out of the image.  Words at or above KREACH
    // read as zero (the page pool, which b6sim does not model); the caller has already
    // clipped to the device's own ceiling.
    void read_bytes(unsigned addr, unsigned byte_off, char *dst, unsigned n) const;

    // The terminal counters, fed by SYS_read/SYS_write on descriptors 0, 1 and 2.  These
    // are real: they count the bytes b6sim actually moved.
    void count_tty_in(unsigned n) { tk_nin += n; }
    void count_tty_out(unsigned n) { tk_nout += n; }

    // The guest's name, for u_comm.  Set once, when a program is loaded.
    void set_comm(const std::string &name);

    //
    // The memory devices.  Guest descriptors are host descriptors everywhere else in
    // b6sim, so a synthetic node borrows a real number by opening /dev/null: `dup',
    // `close' and descriptor inheritance then keep working with no table of their own,
    // and only the four calls below have to know the difference.
    //
    struct DevFd {
        int minor;       // 0 = /dev/mem, 1 = /dev/kmem
        uint64_t offset; // byte offset, as lseek left it
    };
    // Returns 0 for /dev/mem, 1 for /dev/kmem, -1 for anything else.
    static int dev_minor(const std::string &path);
    void dev_add(int fd, int minor) { devs[fd] = { minor, 0 }; }
    DevFd *dev_find(int fd);
    void dev_dup(int oldfd, int newfd);
    void dev_close(int fd) { devs.erase(fd); }

    // The ceiling of each node, in words: /dev/kmem stops where an unmapped access does,
    // /dev/mem covers all of core (kernel/dev/mem.c).
    static unsigned dev_limit(int minor);

private:
    std::vector<Word> image; // KREACH words, the kernel's low core as b6sim pretends it
    std::vector<Entry> table;
    std::map<int, DevFd> devs;

    unsigned proc_addr{};   // word address of proc[0], for u_procp
    unsigned msgbuf_addr{}; // ... and of msgbuf, for msgbufp
    unsigned tk_nin{};
    unsigned tk_nout{};

    // Addresses of the live scalars, so refresh() need not search the table.
    unsigned a_time{}, a_lbolt{}, a_dktime{}, a_tknin{}, a_tknout{};

    unsigned place(const char *name, unsigned words, unsigned bytes, unsigned &next);
    void store(unsigned addr, Word w) { image[addr] = w; }
    void store_bytes(unsigned addr, const char *s, unsigned n);
};

#endif // DUBNA_KERNEL_H
