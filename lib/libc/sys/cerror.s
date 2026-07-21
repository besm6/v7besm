//
// cerror -- the error tail every syscall stub shares, and the `errno' word itself.
//
// The gate reports failure in r14 and success by leaving it zero (kernel/syscall.c,
// cmd/sim/syscall.cpp); there is no carry flag on this machine.  So a stub ends with
//
//      $77 N
//   14 v1m cerror              // r14 != 0: the call failed
//   13 uj
//
// and this is where the failing arm lands.  It is the ONLY place r14 is read: r14 is
// caller-saved and the compiler loads it with the negative argument count before every
// call, so any C statement between the extracode and the test would have destroyed it.
// That is the whole reason sys/ is assembly and not C.
//
// cerror stands in for the stub it was branched to, not called from: the branch is a
// `v1m', not a `vjm', so r13 still holds the STUB's caller's return address and the
// `13 uj' below returns straight to the C code that made the call.
//
// The value the gate leaves in r14 is always one of the 34 that include/errno.h
// defines -- the kernel assigns from its own copy in include/sys/user.h, and b6sim maps
// the host's numbering onto the same list -- so phase 2's sys_errlist needs 35 entries
// and no more.
//
        .text
        .globl  cerror, errno

cerror:
        ita     14                      // A := the errno the gate left in r14
        atx     <errno>                 //   store it, reaching bss with the full 15 bits
        xta     #037777777777777        // A := -1

// The literal is -1 as a C int, which is NOT the 48-bit all-ones that b6as's unary
// minus would produce: an int is 41 bits with its sign in bit 41 and bits 48-42 zero
// (doc/Besm6_Data_Representation.md), and that is the exact word both gates return
// (BITS41 in cmd/sim/besm6_arch.h, `tr->acc = -1' in kernel/syscall.c).  It has to be
// written out inline like this -- an equate would truncate it to 24 bits without a
// diagnostic (doc/Assembler_Manual.md sec 5).

     13 uj

// `errno' lives here rather than in a C file so that every stub that branches to
// cerror drags in its definition with it, and a program that calls no syscall at all
// links neither.  The `< >' escape above is needed for the same reason crt0.s needs it
// for `environ': the segment order is const|text|data|bss, so bss is the one segment a
// large program can push past the 12-bit short address field.
        .bss
errno:
        . = . + 1
