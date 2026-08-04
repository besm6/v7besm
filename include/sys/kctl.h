// The kernel-variable interface: kctl(2), and the kgetsym(3) shorthand over it.
//
// A program that inspects the running system -- ps, dmesg, pstat, iostat -- has to find a
// kernel variable.  Every Unix before this one did it with nlist(3) over the kernel image,
// and THERE IS NO KERNEL IMAGE HERE: root.manifest names no /unix, and kernel/unix.ini has
// the SIMULATOR load one off the build host.  So the kernel carries a small hand-written
// table of the variables it is willing to publish, and this call reads it.  RetroBSD met the
// same wall and answered it through one sysctl(2) node; this is not one -- no MIB, no name
// space, one call.
//
// KCTL_GET copies the variable's VALUE out, so dmesg, iostat and vmstat need no memory device
// and no privilege.  KCTL_STAT hands back the ADDRESS, which a pointer-chaser needs: pstat
// resolves p_textp into an index in text[] and reads on through /dev/kmem, and through
// /dev/mem for a u-area at p_addr (above KREACH, out of /dev/kmem's reach).  PSTAT IS THE
// ONLY PROGRAM LEFT ON THAT LADDER -- ps climbed it too until KCTL_PSINFO made the kernel do
// the walking.  What is worth guarding is the memory; what ps wanted was three columns.
//
// The table itself is kernel-private (kernel/kctl.c).  KCTL_LIST is the only way to see what
// is in it: nothing in user space should hold a second copy of the list.

#ifndef _SYS_KCTL_H
#define _SYS_KCTL_H

// Operations.  KCTL_SET is RESERVED and answers EINVAL: implementing it means a per-entry
// write flag (KCTLF_WR below) and a suser() gate, and neither is here.
//
// TWO OF THESE NAME NOTHING: KCTL_LIST and KCTL_PSINFO ignore `name' and are handled before
// it is read, so a caller passes 0 (lib/test/kctlt.c is the idiom).
#define KCTL_GET    0 // copy the variable's value into buf
#define KCTL_SET    1 // reserved -- EINVAL
#define KCTL_LIST   2 // copy the exported names into buf, KSYMLEN bytes each
#define KCTL_STAT   3 // copy a struct kctlstat into buf
#define KCTL_PSINFO 4 // copy a struct psinfo per proc[] slot into buf

// Width of one KCTL_LIST record, in BYTES -- two words exactly, so a name is at most 11
// characters and its NUL is always in the record.  A caller divides by this to get the count.
#define KSYMLEN 12

// kc_flags.  Only KCTLF_RD is ever set today.
#define KCTLF_RD 01 // readable through KCTL_GET
#define KCTLF_WR 02 // reserved: writable through KCTL_SET

struct kctlstat {
    int kc_addr;  // kernel WORD address -- multiply by NBPW for an lseek on /dev/kmem
    int kc_size;  // size of the variable, in bytes
    int kc_flags; // KCTLF_*
};

// THE REST OF THIS HEADER NAMES KERNEL TYPES, and one host tool cannot have them:
// cmd/sim/params.cpp includes this file by relative path with include/ off its -I, so the
// angle-bracket forms below would find the HOST's -- whose param.h macros collide with the
// kernel's and whose time_t is not one 48-bit word.  That tool defines KCTL_NO_KERNEL_TYPES
// and takes the numbers alone.  Nothing else in the tree defines it.
#ifndef KCTL_NO_KERNEL_TYPES

#include <sys/param.h> // DIRSIZ -- #define-only, which is what makes this safe
#include <sys/types.h> // time_t

// KCTL_PSINFO -- THE SECOND HALF OF kctl("proc", KCTL_GET), and the reason ps(1) is not the
// super-user's program any more.  These three columns are not in struct proc: they live in
// the u-area, and ps used to read them through /dev/kmem and /dev/mem, both mode 0640 and
// root's.  THE FIX WAS NOT TO LOOSEN THOSE MODES AND NOT TO BORROW A UID; IT WAS TO STOP
// ASKING FOR MEMORY.  A command name, a CPU time and a terminal index are what every Unix's
// ps prints to every user; the kernel walks the u-area for the caller and copies out four
// fields, rather than handing over a window onto every process's image.  A setuid ps would
// have been the other answer and would still be wrong, which is why this returns a digest
// and not an address.  cmd/README.md SS8 is the rule and this is its best example.
//
// WHY AN OPERATION AND NOT A ROW, and not a call of its own: every row in kernel/kctl.c is a
// link-time relocation of a real declaration, and a digest computed at the moment of asking
// has no address to relocate.  A system call would have cost a NUMBER, which is permanent and
// externally visible where an operation on a call that already exists is neither.
//
// It deliberately restates not one field of struct proc -- one home for one layout.
//
// ps_pid IS THE JOIN KEY AND THE STALENESS CHECK: between the two calls a process can exit
// and its slot be reused, so compare it with ptab[i].p_pid and skip the row when they differ.
// A row with no readable u-area -- an empty slot, a zombie, a process swapped out -- comes
// back ps_time == 0, ps_ttyn == -1, ps_comm[0] == '\0'; the kernel does not repeat p_stat's
// verdict in a flag of its own, the caller having it already.
struct psinfo {
    int ps_pid;           // proc[i].p_pid when this row was filled -- the JOIN KEY
    time_t ps_time;       // u_utime + u_stime, in ticks at HZ
    int ps_ttyn;          // index of u_ttyp in sc[], or -1 for no controlling terminal
    char ps_comm[DIRSIZ]; // NOT NUL-terminated when the name fills it -- bound it with a
                          // precision, as ps and dcheck and ncheck do
};

#endif // KCTL_NO_KERNEL_TYPES

// int kctl(const char *name, int op, void *buf, int len)
//
// Copies at most `len' bytes into `buf' and returns HOW MANY ARRIVED -- read(2)'s rule, and
// the one rule this call has.  A `len' of 0 copies nothing and returns the number AVAILABLE,
// which is how a caller sizes a buffer; a short `len' truncates rather than failing.  On
// failure: -1, and
//
//      ENOENT  no such name (an empty name matches nothing and is not special)
//      EINVAL  unknown op, KCTL_SET, or a name with no NUL in its first KSYMLEN bytes
//      EFAULT  name or buf unreadable
//
// KCTL_PSINFO fills NPROC records of struct psinfo, one per proc[] slot and in proc[]'s
// order, so the two calls a process-status program makes line up by index.
//
// NOT PRIVILEGED, in any operation.  What is worth guarding is the memory, and /dev/kmem and
// /dev/mem already are; an exported variable's value is a property of the system, not of who
// asked.  RetroBSD's uid-0 requirement is an accident of its framework and not reproduced.
//
// `len' is an int and not a size_t*: an in/out length buys nothing the return value does not,
// and unsigned arithmetic here is a libruntime call per compare.
//
// A KCTL_GET of a pointer-valued variable -- msgbufp is the only one -- yields the raw word,
// and a char * here is a FAT pointer: run it through ptrword() (<sys/param.h>).
//
// Guarded like <sys/times.h>'s block: the kernel's handler is `void kctl(void)' (<sys/systm.h>)
// and the two must never both be in scope.  -DKERNEL reaches every kernel-side translation
// unit, kernel/test/'s programs included.
#ifndef KERNEL
int kctl(const char *name, int op, void *buf, int len);

// The shorthand the pointer-chasers want: the word address of `name', or -1.  A libc routine
// (lib/libc/gen/kgetsym.c) over KCTL_STAT, not a system call of its own.
int kgetsym(const char *name);
#endif

#endif // _SYS_KCTL_H
