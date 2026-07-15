/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/acct.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/inode.h"
#include "sys/proc.h"
#include "sys/seg.h"
#include "sys/map.h"
#include "sys/reg.h"
#include "sys/buf.h"
// clang-format on

extern int phymem;

int maxmem; /* actual max memory per process */

extern int uarea[]; /* the u-area (076000) as a flat word array; see besm6.S */

/*
 * Base of the per-process kernel stack: the top of struct user, which grows up
 * to 0100000.  besm6.S:_start seeds the stack pointer (r15) here at boot; a
 * context switch reloads r15 from the saved label thereafter.
 *
 * u_stack is the last member of struct user, so its word offset is the struct's
 * word size minus one.  We spell the address as &uarea[that] rather than the
 * obvious &u.u_stack because b6cc will not take the address of a struct member in
 * a static initializer -- only &array[const] folds a symbol+offset into the
 * relocation.  uarea aliases u at the same absolute address, so this is that
 * word: UBASE + wordsizeof(struct user) - 1 (~076214).
 */
int *const ustkbase = &uarea[sizeof(struct user) / sizeof(int) - 1];

/*
 * Icode is the hex bootstrap
 * program executed in user mode
 * to bring up the system.
 */
int icode[] = {
    0x186a106a, /* push $initp; push $init */
    0xbb8006a,  /* push $0; mov $11,eax */
    0xcd000000, /* int $0x30 */
    0xfeeb30,   /* jmp . */
    0x18,       /* initp: init; 0 */
    0x0,        /* */
    0x6374652f, /* init: </etc/init\0> */
    0x696e692f, /* */
    0x74        /* */
};
int szicode = sizeof(icode);

/*
 * Machine-dependent startup code
 */
void startup()
{
    /*
     * Free all of core above the kernel.  Pages 0..31 -- words 0 through
     * 0100000 -- are the kernel image and the u-area at 076000, and word
     * 0100000 is the first free word.  The first page of the pool is the
     * u-area home of proc[0], which main() claims right after us, so the
     * pool proper starts one page higher.
     *
     * phymem is the size of physical memory, in words; main() sets it just
     * before calling us (kernel/main.c).
     */
    maxmem = phymem - (NPAGE * PGSZ + USIZE);
    mfree(coremap, maxmem, NPAGE * PGSZ + USIZE);
    printf("mem = %d words\n", maxmem);
    if (MAXMEM < maxmem)
        maxmem = MAXMEM;
    mfree(swapmap, nswap, 1);
    swplo--;
}

/*
 * set up a physical address
 * into users virtual address space.
 */
void sysphys()
{
    if (!suser())
        return;
    u.u_error = EINVAL;
}

/*
 * Start the clock.
 */
void clkstart()
{
    /* TODO: BESM-6 interval timer.  The x86 8253 PIT and the CMOS RTC (which set
     * `time' from the wall clock) are gone -- they were x86 programmed I/O with no
     * BESM-6 analogue.  Until the machine timer is programmed, just open the
     * interrupt level. */
    spl0();
}

/*
 * Let a process handle a signal by simulating a call
 */
void sendsig(caddr_t p, int signo)
{
    register unsigned n;

    n = u.u_ar0[ESP] - 4;
    grow(n);
    suword((caddr_t)n, u.u_ar0[EIP]);
    u.u_ar0[ESP] = n;
    u.u_ar0[EFL] &= ~TBIT;
    u.u_ar0[EIP] = (int)p;
}

