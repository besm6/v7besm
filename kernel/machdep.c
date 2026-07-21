/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/types.h"
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

extern int edata[], end[]; /* bss spans [edata, end); b6ld defines both boundaries */

/*
 * Publish the physical memory size (words), which startup() frees into the
 * coremap.  The kernel runs unmapped (32 Kword reach) and cannot probe the
 * 512 Kword store; a real scan would need the MMU, so we take the fixed SIMH
 * MEMSIZE.
 */
int phymem = 512 * 1024;

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
     * Clear bss before anything reads it.  This is _start's work, done here
     * because the size -- `end - edata', a difference of two linker externals --
     * is not expressible in b6as; in C the compiler emits the pointer subtraction.
     * Nothing above has touched bss yet: _start is register-only, main() calls us
     * first, and so is wzero().  sizeof(int) == 1 word, so `end - edata' is a word
     * count, wzero()'s unit.  phymem is initialized data, so the clear spares it.
     *
     * SIMH starts every word at zero, so on the simulator the clear is redundant;
     * it is kept here, guarded, for the day the kernel boots on real hardware.
     */
#define ON_SIMH
#ifndef ON_SIMH
    wzero(edata, end - edata);
#endif

    /*
     * Free all of core above the kernel.  Pages 0..31 -- words 0 through
     * 0100000 -- are the kernel image and the u-area at 076000, and word
     * 0100000 is the first free word.  The first page of the pool is the
     * u-area home of proc[0], which main() claims right after us, so the
     * pool proper starts one page higher.
     */
    printf("phys mem  = %d kbytes\n", (phymem * NBPW) >> 10);
    maxmem = phymem - (NPAGE * PGSZ + USIZE);
    mfree(coremap, maxmem, NPAGE * PGSZ + USIZE);
    /* Print before the clamp below: past it maxmem means core per process. */
    printf("user mem  = %d kbytes\n", (maxmem * NBPW) >> 10);
    if (MAXMEM < maxmem)
        maxmem = MAXMEM;
    printf("swap size = %d kbytes\n", (nswap * BSIZE) >> 10);
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
     * Nothing seeds `time' either: this machine has no clock-calendar a program can
     * read, so the epoch still starts at 0 until something sets the date.
     */
    __besm6_mod(MOD_GRPCLR, ~GRP_TIMER);
    spl0();
}

/*
 * Let a process handle a signal by simulating a call
 */
void sendsig(caddr_t p, int signo)
{
    register int n;

    /*
     * The user stack grows UP from 070000 (exec seeds it there; see the arg-block
     * comment in sys1.c), and r15 is a WORD index naming the first free slot -- so a
     * push stores AT r15 and then increments it by one word, not by a byte count.
     *
     * This is the minimal push: one word, the address to resume at.  A full signal
     * frame -- the saved accumulator, R, and a sigreturn path back through it -- is
     * still unbuilt, and nothing exercises signal delivery yet.  There is no EFL/TBIT
     * to clear either: single-step is the address-break registers М034/М035, not a flag.
     */
    n = u.u_ar0[R15];
    grow(n >> PGSH); /* the page r15 points into may not be mapped yet */
    suword((caddr_t)n, u.u_ar0[RET]);
    u.u_ar0[R15] = n + 1;
    u.u_ar0[RET] = (int)p;
}
