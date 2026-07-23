// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The signal frame: the two halves of "let a process handle a signal by simulating
// a call" -- sendsig(), which builds the frame and enters the handler, and
// sigret(), the syscall the frame returns through.
//
// This is a SEPARATE FILE, not part of machdep.c, for the reason syscall.c is
// separate from trap.c and brz.s from besm6.S: kernel/test/usig links the real
// frame builder against a hand-built process and runs a whole delivery on the
// machine.  It cannot link machdep.c, which also holds startup() and drags in
// mfree(), the coremap and printf(), and with them the rest of the kernel.
//
// The design, in full, is in doc/Unix_Context_Switch.md; the essentials:
//
//   * THE FRAME IS THE reg.h FRAME.  All 21 words of `struct trap' go on the user
//     stack unchanged, so nothing here has to decide what a handler may clobber:
//     r1-r7 are callee-saved and survive on their own, but a handler that longjmps
//     out (lib/libc/gen/sleep.c does exactly that) never restores anything, and the
//     frame is what makes that safe.
//
//   * THE RETURN PATH IS ONE PLANTED INSTRUCTION.  A handler is an ordinary C
//     function and returns with `13 uj', so r13 must name something executable.  It
//     names one word pushed just above the frame, holding the `$77 SYS_sigret'
//     that kernel/besm6.S assembled as `sigcode' -- b6as writes the encoding, not C.
//
//   * A USER-MODE RETURN PATH COULD NOT WORK.  Which half of a word to resume at
//     lives in SPSW (SPSW_RIGHT_INSTR) and only `выпр' reloads it, so a signal taken
//     on a right-half instruction -- every fault can be -- can only be resumed by the
//     kernel.  That is why sigret() is a syscall and not a few instructions in
//     libc, and why libc needs no trampoline of its own at all.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

// kernel/besm6.S: one word, `$77 SYS_sigret', copied to the user stack below.
// An array so that the name is the address, as with edata[]/end[] in machdep.c.
extern int sigcode[];

// Let a process handle a signal by simulating a call.
//
// `p' is the handler (u.u_signal[signo], already vetted by psig()), and the frame is
// built on the USER stack, which grows UP from 070000 -- exec seeds it there; see the
// arg-block comment in sys1.c.  r15 is a WORD index naming the first free slot, so a
// push stores AT r15 and steps it by one word, not by a byte count:
//
//      n .. n+20   the saved reg.h frame (copyout, one call)
//      n+21        `$77 SYS_sigret'  -- the return trampoline
//      n+22        r15 as the handler sees it
//
// The handler is entered by the ordinary calling convention for a one-argument
// function (doc/Besm6_Calling_Conventions.md): the last -- here the only -- argument
// in the accumulator, r14 the negative argument count, r13 the return address.
void sendsig(caddr_t p, int signo)
{
    register struct trap *tr = (struct trap *)u.u_ar0;
    register int n;

    n = tr->r15;

    // The page the frame lands in may not be mapped yet.  Grow for the LAST word of
    // it: the stack is contiguous from USTKPAGE up, so growing to the higher page
    // covers a frame that straddles two, and grow() is a no-op when it is mapped
    // already.
    grow((n + NREGFRAME) >> PGSH);

    // Copy the frame out BEFORE any of the writes below: `tr' is the live frame, in
    // place, and what goes on the stack must be the context the handler interrupted.
    //
    // A failure here means the stack could not grow, and there is nothing to resume:
    // the frame is half-written and the process cannot be told about it.  v7 ignored
    // the return value -- on the PDP-11 it was pushing two words, not twenty-two --
    // and would resume a process whose saved context was never stored.  Kill it
    // instead; exit() does not come back.
    if (copyout((caddr_t)tr, (caddr_t)n, wtob(NREGFRAME)) < 0 ||
        suword((caddr_t)(n + NREGFRAME), sigcode[0]) < 0)
        exit(SIGKIL);

    tr->acc  = signo;             // the handler's one argument
    tr->r14  = -1;                // argument count, negative, per the convention
    tr->r13  = n + NREGFRAME;     // return address: the planted extracode
    tr->r15  = n + NREGFRAME + 1; // the handler's own frame starts above it
    tr->ret  = (int)p;            // and `выпр' enters the handler
    tr->creg = 0;                 // no address modifier armed
    tr->rreg = RREG_C;            // the AU mode word compiled code runs in.  NOT zero,
                                  //   which is what setregs() leaves after an exec:
                                  //   there `ntr 7' is crt0's first instruction, and a
                                  //   handler has no crt0 in front of it -- the first
                                  //   b$ helper it calls would run in the wrong ω.

    // Enter at the LEFT half of the handler's first word with nothing pending.  A
    // fault frame can carry any of these -- SPSW_RIGHT_INSTR says which half faulted,
    // SPSW_MOD_RK that the interrupted instruction was to be modified by M[16] --
    // and `выпр' would apply them to the handler's first instruction.  They are saved
    // in the frame and come back through sigret().
    tr->spsw &= ~(SPSW_RIGHT_INSTR | SPSW_NEXT_RK | SPSW_MOD_RK | SPSW_MOD_RR);
}

// sigret() -- the syscall the planted trampoline issues, and the only way back
// out of a handler.  It takes NO arguments: the handler's epilogue leaves r15 exactly
// as sendsig() set it (a C function pops what it pushed), so the frame is the 21
// words below the trampoline word, at r15 - (NREGFRAME + 1).
//
// The restore is a copyin over the LIVE frame, which the gate's `intret' epilogue
// reloads into the registers on the way out -- so this returns the user to the
// instruction it was interrupted at, in the right half of the right word, with the
// accumulator, R, Y, M[16] and every index register as they were.
//
// It is reachable by any user program that cares to issue `$77 45' with anything at
// all below r15, so nothing here may be taken on trust.  copyin() validates the range
// through useracc(), and SPSW -- the one word in the frame that is not just user
// state -- is masked: only the two bits `выпр' takes from it that affect user
// execution are the caller's to set (SPSW_USER, reg.h).  The mode bits come from the
// live frame, which is a genuine user-mode SPSW, so a forged frame cannot buy
// supervisor mode, an unmapped address space or a blocked interrupt level.
void sigret()
{
    register struct trap *tr = (struct trap *)u.u_ar0;
    struct trap frame;

    if (copyin((caddr_t)(tr->r15 - (NREGFRAME + 1)), (caddr_t)&frame, wtob(NREGFRAME)) < 0) {
        u.u_error = EFAULT;
        return;
    }
    frame.spsw = (frame.spsw & SPSW_USER) | (tr->spsw & ~SPSW_USER);
    *tr        = frame;

    // Tell syscall() to keep its hands off the frame: its usual return through
    // ACC/r12/r14 would overwrite three registers just restored, and r14 is live at
    // every delivery point -- it is the errno a libc stub is about to test.
    u.u_justreturn = 1;
}
