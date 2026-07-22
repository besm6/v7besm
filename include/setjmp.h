// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Non-local goto.
//
// The buffer is int[8] as it is on every v7 machine, but what goes into it is this
// machine's, and lib/libc/gen/setjmp.s is the only thing that may touch it:
//
//      0       r13     the return address into setjmp's caller
//      1       r15     the stack pointer at the call
//      2,3,4   r5,r6,r7    the callee-saved registers compiled code actually uses
//      5       R       the AU mode register, in bits 47-42 as RTE leaves it
//      6,7     -- unused; a signal mask when phase 6 needs one --
//
// `int' and not `char', so a jmp_buf is a plain word address rather than a fat pointer
// and goes straight into an index register.
//
// longjmp is deliberately NOT _Noreturn, which is this header's one deviation from
// C11 §7.13.2.1: declaring it so would turn every call site into a tail `uj' for no
// gain here, and no caller of longjmp expects it back anyway.
#ifndef _SETJMP_H
#define _SETJMP_H

typedef int jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif // _SETJMP_H
