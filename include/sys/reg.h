/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include "sys/besm6dev.h" /* SPSW_* mode-word bits */

/*
 * Location of the user's saved registers on the kernel-stack trap frame.
 * Usage is u.u_ar0[XX].  u.u_ar0 points at frame word 0 (the accumulator), so
 * every index below is a plain, non-negative word index.  
 * sizeof(int) == 6 == one 48-bit word, so an index is a word index and 
 * the struct-field order below matches the index order.
 */

#define ACC  0 /* accumulator: primary syscall result (was EAX) */
#define RREG 1 /* R   -- ALU mode word (omega + the NTR suppress bits) */
#define RMR  2 /* Y (РМР) -- younger-bits register */
#define RET  3 /* return address: IRET (И33) for a fault, ERET (И32) for an extracode */
#define SPSW 4 /* SPSW -- saved mode word (М027) */
#define CREG 5 /* М16 = C register M[16] (M[020]), the address modifier */

/* general registers -- stored DESCENDING, М15..М1 at offsets 6..20 */
#define R15 6  /* stack pointer */
#define R14 7  /* argument-count / errno register (BESM-6 syscall ABI) */
#define R13 8
#define R12 9
#define R11 10
#define R10 11
#define R9  12
#define R8  13
#define R7  14
#define R6  15
#define R5  16
#define R4  17
#define R3  18
#define R2  19
#define R1  20

#define NREGFRAME 21 /* words in the trap frame */

/*
 * Semantic aliases used by the C readers.  These follow the BESM-6 syscall ABI
 * (doc/Besm6_Calling_Conventions.md, cmd/sim/syscall.cpp):
 * a syscall result is ACC, an error is errno-in-r14 (0 on success) with no
 * carry flag, and v7's second result (pipe/wait/getpid/getuid/getgid) is r12.
 */
#define R_ERRNO R14 /* syscall errno; 0 == success.  There is no carry bit. */
#define R_VAL2  R12 /* second syscall result (pipe/wait/getpid/getuid/getgid) */

/*
 * The saved mode word SPSW carries РежЭ|РежПр (RUU_EXTRACODE|RUU_INTERRUPT,
 * octal 014); both bits clear iff the interrupted context was user mode.  That test
 * is the whole of USERMODE().  See doc/Unix_Context_Switch.md and doc/Memory_Mapping.md.
 */
#define SPSW_MODE      (SPSW_EXTRACODE | SPSW_INTERRUPT) /* РежЭ | РежПр */
#define USERMODE(spsw) (((spsw) & SPSW_MODE) == 0)

/*
 * BASEPRI(): were we interrupted ABOVE base priority?  Note the sense -- v7's name
 * reads backwards.  True means "the interrupted code was holding a raised spl, so do
 * not run callouts on top of it"; clock() takes the short exit on true.
 *
 * On this machine it is permanently false, and that is a fact about the hardware, not
 * a stub.  The BESM-6 has no priority hierarchy, so this kernel has exactly two levels
 * rather than the PDP-11's eight: interrupts enabled and interrupts blocked (intr.c).
 * The level is БлПр, and every splN above spl0 SETS it -- so code holding a raised spl
 * cannot be interrupted at all, and anything clock() interrupts was, by construction,
 * running at base priority.  There is no raised level for it to find.
 *
 * Note that the other half of the delivery condition, ГРП & МГРП nonzero, says nothing
 * about the level and must not be read as if it did: МГРП is a source enable that
 * drivers arm and disarm per exchange (mgrpon()/mgrpoff()), independently of spl.
 */
#define BASEPRI(x) (0)

struct trap {
    int acc;  /*  0  ACC */
    int rreg; /*  1  R */
    int rmr;  /*  2  Y */
    int ret;  /*  3  return address (IRET for a fault, ERET for an extracode) */
    int spsw; /*  4  saved program status word */
    int creg; /*  5  MOD = C register M[16] */
    int r15;  /*  6  М15  (SP) */
    int r14;  /*  7  М14  (errno / arg count) */
    int r13;  /*  8  М13 */
    int r12;  /*  9  М12 */
    int r11;  /* 10  М11 */
    int r10;  /* 11  М10 */
    int r9;   /* 12  М9 */
    int r8;   /* 13  М8 */
    int r7;   /* 14  М7 */
    int r6;   /* 15  М6 */
    int r5;   /* 16  М5 */
    int r4;   /* 17  М4 */
    int r3;   /* 18  М3 */
    int r2;   /* 19  М2 */
    int r1;   /* 20  М1 */
};
