// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

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

extern int edata[], end[]; // bss spans [edata, end); b6ld defines both boundaries

// Publish the physical memory size (words), which startup() frees into the
// coremap.  The kernel runs unmapped (32 Kword reach) and cannot probe the
// 512 Kword store; a real scan would need the MMU, so we take the fixed SIMH
// MEMSIZE.
int phymem = 512 * 1024;

int maxmem; // actual max memory per process

// Base of the per-process kernel stack: the top of struct user, which grows up
// to 0100000.  besm6.S:_start seeds the stack pointer (r15) here at boot; a
// context switch reloads r15 from the saved label thereafter.
//
// u_stack is the last member of struct user, so this points at UBASE +
// wordsizeof(struct user) - 1 (~074214).  b6cc now folds &u.u_stack[0] --
// a symbol+offset -- into the static relocation, so we can spell it directly.
//
// 884 of the words above it are SAVED (they are inside the USIZE the context
// switch copies, which ends at 075777); the 1024 above that are overflow, and
// running there is only safe as long as the process does not leave the CPU.
// The rule is at UBASE in <sys/param.h>.
int *const ustkbase = &u.u_stack[0];

// The geometry that rule depends on: one saved page, one overflow page, ending
// exactly at the top of the unmapped reach.  A negative array size buys nothing
// here (b6cc accepts one), but _Static_assert has teeth.  It cannot live in
// <sys/param.h>, which kernel/uarea.S #includes as assembly.
_Static_assert(UBASE + 2 * USIZE == 0100000,
               "the u-area must be one saved page plus one overflow page, ending at 0100000");

// icode[] -- the user bootstrap v7 kept here as a hex blob -- is BESM-6 assembly now, in
// kernel/besm6.S beside sigcode, for the reason sigcode is there: nothing in this port writes
// down an opcode encoding, and `$77 SYS_exec' takes its number from <sys/syscall.h> like every
// other caller.  main() copies it into process 1's image and _start enters it.

// Machine-dependent startup code
void startup()
{
    // Clear bss before anything reads it.  This is _start's work, done here
    // because the size -- `end - edata', a difference of two linker externals --
    // is not expressible in b6as; in C the compiler emits the pointer subtraction.
    // Nothing above has touched bss yet: _start is register-only, main() calls us
    // first, and so is wzero().  sizeof(int) == 1 word, so `end - edata' is a word
    // count, wzero()'s unit.  phymem is initialized data, so the clear spares it.
    //
    // SIMH starts every word at zero, so on the simulator the clear is redundant;
    // it is kept here, guarded, for the day the kernel boots on real hardware.
#define ON_SIMH
#ifndef ON_SIMH
    wzero(edata, end - edata);
#endif

    // Free all of core above the kernel.  Pages 0..31 -- words 0 through
    // 0100000 -- are the kernel image, the buffer cache and the two u-area
    // pages at 074000, and word 0100000 is the first free word.  The first
    // page of the pool is the u-area home of proc[0], which main() claims
    // right after us, so the pool proper starts one page higher.  Only USIZE
    // is subtracted, not the whole u-area: the overflow page is nobody's home.
    printf("phys mem  = %d kbytes\n", (phymem * NBPW) >> 10);
    maxmem = phymem - (NPAGE * PGSZ + USIZE);
    mfree(coremap, maxmem, NPAGE * PGSZ + USIZE);
    // Print before the clamp below: past it maxmem means core per process.
    printf("user mem  = %d kbytes\n", (maxmem * NBPW) >> 10);
    if (MAXMEM < maxmem)
        maxmem = MAXMEM;
    printf("swap size = %d kbytes\n", (nswap * BSIZE) >> 10);
    mfree(swapmap, nswap, 1);
    swplo--;
}

// set up a physical address
// into users virtual address space.
void sysphys()
{
    if (!suser())
        return;
    u.u_error = EINVAL;
}

// Start the clock.
void clkstart()
{
    // There is no timer to program.  The BESM-6 interval timer free-runs from reset
    // and re-arms itself, raising GRP_TIMER (ГРП bit 40) 250 times a second -- see
    // HZ in <sys/param.h>.  All the kernel can do is mask it, which is what spl does.
    //
    // So dismiss whatever the machine accumulated while we were booting -- at 250 Hz
    // there is certainly a tick pending -- and open the interrupt level.
    //
    // Nothing seeds `time' either: this machine has no clock-calendar a program can
    // read, so the epoch still starts at 0 until something sets the date.
    __besm6_mod(MOD_GRPCLR, ~GRP_TIMER);
    spl0();
}

// sendsig() -- the signal frame -- was here.  It is kernel/sendsig.c now, together
// with the sigreturn() that returns through it: kernel/test/usig links the pair
// against a hand-built process and runs a whole delivery on the machine, and it
// cannot link this file, whose startup() drags in mfree(), the coremap and printf().
