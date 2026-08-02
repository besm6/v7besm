// The kernel-variable interface: kctl(2), and the kgetsym(3) shorthand over it.
//
// A program that inspects the running system -- ps, dmesg, pstat, iostat -- has to find a
// kernel variable.  Every Unix before this one did it with nlist(3) over the kernel image,
// and THERE IS NO KERNEL IMAGE HERE: root.manifest names no /unix, and kernel/unix.ini has
// the SIMULATOR load one off the build host.  So the kernel carries a small hand-written
// table of the variables it is willing to publish, and this call reads it.  RetroBSD met the
// same wall (its kernel lives in flash) and answered it the same way, through one sysctl(2)
// node; there is no sysctl here and this is not one -- no MIB, no name space, one call.
//
// It goes further than a name lookup in the one direction that pays: KCTL_GET copies the
// variable's VALUE out, so dmesg and iostat need no memory device and no privilege at all.
// KCTL_STAT hands back the ADDRESS, which is what the pointer-chasers need -- pstat resolves
// p_textp into an index in text[] and u_ttyp into one in sc[], and no amount of value copying
// substitutes for the base address.  Those two read on through /dev/kmem, and through
// /dev/mem for a u-area at p_addr, which is above KREACH and out of /dev/kmem's reach
// (kernel/dev/mem.c).  lib/test/memt.c is that ladder's first rung.
//
// The table itself is kernel-private (kernel/ksym.c).  KCTL_LIST is the only way to see what
// is in it, which is deliberate: nothing in user space should hold a second copy of the list.

#ifndef _SYS_KCTL_H
#define _SYS_KCTL_H

// Operations.  KCTL_SET is RESERVED and answers EINVAL: nothing yet needs to write a kernel
// variable, and holding the number costs nothing.  Implementing it means a per-entry write
// flag (KCTLF_WR below) and a suser() gate, and neither is here.
#define KCTL_GET  0 // copy the variable's value into buf
#define KCTL_SET  1 // reserved -- EINVAL
#define KCTL_LIST 2 // copy the exported names into buf, KSYMLEN bytes each
#define KCTL_STAT 3 // copy a struct kctlstat into buf

// Width of one KCTL_LIST record, in BYTES -- two words exactly, so a name is at most 11
// characters and its terminating NUL is always in the record.  A caller divides the return
// value by this to get the count; there is nothing to scan for.
#define KSYMLEN 12

// kc_flags.  Only KCTLF_RD is ever set today.
#define KCTLF_RD 01 // readable through KCTL_GET
#define KCTLF_WR 02 // reserved: writable through KCTL_SET

struct kctlstat {
    int kc_addr;  // kernel WORD address -- multiply by NBPW for an lseek on /dev/kmem
    int kc_size;  // size of the variable, in bytes
    int kc_flags; // KCTLF_*
};

// int kctl(const char *name, int op, void *buf, int len)
//
// Copies at most `len' bytes into `buf' and returns HOW MANY ARRIVED -- read(2)'s rule, and
// the one rule this call has.  A `len' of 0 copies nothing and returns the number of bytes
// AVAILABLE, which is how a caller sizes a buffer; a short `len' truncates rather than
// failing, so completeness is a comparison the caller makes, exactly as it does after a
// read().  On failure: -1, and
//
//      ENOENT  no such name (an empty name matches nothing and is not special)
//      EINVAL  unknown op, KCTL_SET, or a name with no NUL in its first KSYMLEN bytes
//      EFAULT  name or buf unreadable
//
// KCTL_LIST does not look at `name' at all.
//
// NOT PRIVILEGED.  What is worth guarding is the memory, and /dev/kmem and /dev/mem already
// are (mode 0640, root); an exported variable's value is a property of the system, not of who
// asked.  RetroBSD's uid-0 requirement is an accident of its framework -- it smuggles the
// lookup key through sysctl's "new value" argument, which the dispatcher then reads as a
// write -- and is not worth reproducing.
//
// `len' is an int and not a size_t*, which is what sysctl would have used.  An in/out length
// buys nothing the return value does not, and unsigned arithmetic on this machine is a
// libruntime call per compare (b$ult, b$usub; the note at the head of kernel/dev/mem.c).
//
// A KCTL_GET of a pointer-valued variable -- msgbufp is the only one -- yields the raw word,
// and a char * here is a FAT pointer: run it through ptrword() (<sys/param.h>) to get the
// word address.  Nothing in the kernel unpacks it for you, because nothing else needs it.
//
// Guarded like <sys/times.h>'s block and for its reason: the kernel's syscall handler is
// `void kctl(void)' (declared in <sys/systm.h>) and the two must never both be in scope.
// -DKERNEL reaches every kernel-side translation unit, kernel/test/'s programs included.
#ifndef KERNEL
int kctl(const char *name, int op, void *buf, int len);

// The shorthand the pointer-chasers want: the word address of `name', or -1.  A libc routine
// (lib/libc/gen/kgetsym.c) over KCTL_STAT, not a system call of its own.
int kgetsym(const char *name);
#endif

#endif // _SYS_KCTL_H
