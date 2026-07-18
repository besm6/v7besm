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
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

extern int phymem;

int maxmem; /* actual max memory per process */

/*
 * Base of the per-process kernel stack: the top of struct user, which grows up
 * to 0100000.  besm6.S:_start seeds the stack pointer (r15) here at boot; a
 * context switch reloads r15 from the saved label thereafter.
 *
 * u_stack is the last member of struct user, so this points at UBASE +
 * wordsizeof(struct user) - 1 (~076214).  b6cc now folds &u.u_stack[0] --
 * a symbol+offset -- into the static relocation, so we can spell it directly.
 */
int *const ustkbase = &u.u_stack[0];

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
    /*
     * There is no timer to program.  The BESM-6 interval timer free-runs from reset
     * and re-arms itself, raising GRP_TIMER (ГРП bit 40) 250 times a second -- see
     * HZ in <sys/param.h>.  All the kernel can do is mask it, which is what spl does.
     *
     * So dismiss whatever the machine accumulated while we were booting -- at 250 Hz
     * there is certainly a tick pending -- and open the interrupt level.
     *
     * The x86 8253 PIT is gone, and so is the CMOS RTC that seeded `time' from the
     * wall clock: this machine has no clock-calendar a program can read, so the epoch
     * still starts at 0 until something sets the date.
     */
    __besm6_mod(MOD_GRPCLR, ~GRP_TIMER);
    spl0();
}

/*
 * Let a process handle a signal by simulating a call
 */
void sendsig(caddr_t p, int signo)
{
    register unsigned n;

    /*
     * TODO 17: the BESM-6 user stack grows UP (exec seeds it at 070000), so the
     * x86 "SP - 4" push direction and the whole signal-frame layout must be
     * redone.  Kept compiling here; sendsig is not exercised until the signal
     * delivery path lands.  There is no EFL/TBIT to clear -- single-step is the
     * address-break registers М034/М035, not a flag.
     */
    n = u.u_ar0[R15] - 4;
    grow(n);
    suword((caddr_t)n, u.u_ar0[RET]);
    u.u_ar0[R15] = n;
    u.u_ar0[RET] = (int)p;
}
