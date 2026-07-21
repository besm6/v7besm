//
// setjmp/longjmp -- the non-local goto.
//
//      int  setjmp(jmp_buf env);       -- returns 0 direct, val when longjmp'd to
//      void longjmp(jmp_buf env, int val);
//
// v7's is x86 assembly and the c-compiler's library has none at all, so this one is
// written from the calling convention rather than ported.  It is the same problem as
// save()/resume() in kernel/switch.s one level down, and follows that file's shape;
// what it does NOT need is everything that made resume() hard -- no u-area to copy,
// no address space to swap, no interrupts to mask.
//
// THE BUFFER.  jmp_buf is int[8] (include/setjmp.h), where the slot layout is also
// written down; `int *' is a plain word address, not a fat pointer, so the argument
// goes straight into an index register.
//
//      0       r13     the return address into setjmp's caller
//      1       r15     the stack pointer at the call
//      2,3,4   r5,r6,r7    the callee-saved set the compiler actually uses
//      5       R       the AU mode register, in bits 47-42 as RTE leaves it
//      6,7     -- unused --
//
// WHY ONLY r5-r7 OF THE CALLEE-SAVED SET.  The convention says r1-r7 must be preserved
// (doc/Besm6_Calling_Conventions.md), but b$save/b$ret save exactly r5, r6 and r7, and
// compiled code allocates nothing below r5: what appears in a disassembly is r6 and r7
// (the parameter and auto pointers), r8-r12 as expression scratch, and r13/r14/r15.  A
// function that clobbered r1-r4 across a call would already break an ordinary return,
// so there is nothing here for setjmp to save that b$save does not.
//
// WHY R IS SAVED ANYWAY.  kernel/switch.s argues that it need not be: the ABI fixes
// R = 7 at every call boundary, so the R a longjmp is entered with is by construction
// the R its setjmp is entitled to return with.  That argument is exactly as good here
// and the slot is kept regardless, because phase 6 will longjmp out of a signal
// trampoline -- and a trampoline is not a call boundary.  It costs one word.
//
// NEITHER ROUTINE HAS A PROLOGUE.  setjmp has one parameter, so it arrives in the
// accumulator, nothing is pushed and r15 at entry is already the caller's: a callee
// whose arguments were all in registers pops nothing.  longjmp never returns, so its
// one pushed argument is never popped -- r15 comes from the buffer.
//
        .text
        .globl  setjmp, longjmp

// ----------------------------------------------------------------------------
// int setjmp(jmp_buf env)
// ----------------------------------------------------------------------------

setjmp:
        ati     14              // r14 := &env.  DECIMAL 14, as in kernel/switch.s: a
                                //   bare literal is decimal and a leading 0 makes it
                                //   octal.  `ati' keeps only the low 15 bits.  r14 is
                                //   the argument-count register and is dead by now.
        ita     13
     14 atx     0               // slot 0 := r13, the caller's return address
        ita     15
     14 atx     1               // slot 1 := r15, the stack as the caller left it
        ita     5
     14 atx     2
        ita     6
     14 atx     3
        ita     7
     14 atx     4               // slot 4 := r7
        rte     077             // A := R in bits 47-42 (EA masks R; 077 takes all six)
     14 atx     5
        xta                     // A := 0 -- the direct return.  Word 0 reads as zero,
                                //   which is the idiom the runtime helpers use too
                                //   (b$lt in the c-compiler's library).
     13 uj

// ----------------------------------------------------------------------------
// void longjmp(jmp_buf env, int val)
// ----------------------------------------------------------------------------
//
// Two arguments: `env' is pushed at r15-1 and `val' is in the accumulator.  Every
// value being restored is an index register, and an index register can be loaded from
// memory WITHOUT the accumulator -- `wtc' sets the C register from the low 15 bits of
// a word and `vtm' then computes M[i] = 0 + C.  So val stays in the accumulator from
// entry to exit: no bss cell to park it in (which is what resume() had to do for a
// 19-bit physical address), and nothing written back into the caller's buffer.
//
// The wtc also masks for free: it takes the low 15 bits, so a slot could carry a fat
// pointer's marker bit and still land correctly in an index register.

longjmp:
     15 wtc     -1              // C := the pushed argument, &env
     14 vtm     0               // r14 := &env, without touching the accumulator
     14 wtc     2
      5 vtm     0               // r5 := slot 2
     14 wtc     3
      6 vtm     0
     14 wtc     4
      7 vtm     0               // r7 := slot 4
     14 wtc     1
     15 vtm     0               // r15 := the stack pointer setjmp was called with;
                                //   the argument pushed above it is thereby dropped
     14 wtc     0
     13 vtm     0               // r13 := the return address inside setjmp's caller

        u1a     ljgo            // val != 0: hand it back unchanged.  Tested BEFORE the
                                //   mode register is restored, so the branch reads the
                                //   omega it was entered with -- logical, R = 7, where
                                //   the condition is A != 0.
        xta     #01             // longjmp(env, 0) must return 1, not 0
ljgo:
     14 xtr     5               // R := the saved mode word (bits 47-42 of the slot).
                                //   Does not touch the accumulator either.
     13 uj                      // ... and lands in setjmp's caller, not in ours
