/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

/*
 * Location of the user's saved registers on the kernel-stack trap frame.
 * Usage is u.u_ar0[XX].  u.u_ar0 points at frame word 0 (the accumulator), so
 * every index below is a plain, non-negative word index -- unlike the x86,
 * whose u_ar0 was anchored mid-frame at EAX and reached the lower registers
 * with negative offsets.  sizeof(int) == 6 == one 48-bit word, so an index is
 * a word index and the struct-field order below matches the index order.
 *
 * The layout is Dubna's canonical per-process register-save block, verbatim
 * (doc/Context_Switch.md sec 2), so the frame our gates fill lines up slot for
 * slot with the reference machine:
 *
 *   0 ACC | 1 R | 2 РМР | 3 ИРЕТ | 4 ЭРЕТ | 5 СПСВ | 6..21 М16..М1
 *
 * Two things follow from copying Dubna: the register file is stored
 * DESCENDING -- М16 (which is the C register / address modifier) at offset 6,
 * down to М1 at offset 21, so r15 is at 7 and r1 at 21 -- and there are TWO
 * distinct return slots, ИРЕТ (interrupt/fault, resumed via `выпр'/`3 ij') and
 * ЭРЕТ (extracode, resumed via `2 ij').  ГРП is NOT framed: the fault cause is
 * read live via __besm6_mod(MOD_GRP,0) in trap() (task 15c), which is what
 * Dubna does too.  errno needs no slot -- it is r14 (М14), an alias below.
 *
 * The gate that FILLS this frame and the epilogue that RELOADS the registers
 * from it are tasks 15c (the 0500 fault gate) and 15d (the 0577 syscall gate);
 * the async 0501 external-interrupt gate keeps its own static save area
 * (besm6.S, task 15a).  This header only names the slots -- nothing here runs
 * yet.
 */

#define ACC  0 /* accumulator: primary syscall result (was EAX) */
#define RREG 1 /* R   -- ALU mode word (omega + the NTR suppress bits) */
#define RMR  2 /* РМР -- younger-bits register */
#define IRET 3 /* ИРЕТ -- interrupt / fault return address (И33) */
#define ERET 4 /* ЭРЕТ -- extracode return address (И32) */
#define SPSW 5 /* СПСВ -- saved mode word (М027) */
#define CREG 6 /* М16 = C register M[020], the address modifier */

/* general registers -- stored DESCENDING, М15..М1 at offsets 7..21 */
#define R15 7  /* stack pointer */
#define R14 8  /* argument-count / errno register (BESM-6 syscall ABI) */
#define R13 9
#define R12 10
#define R11 11
#define R10 12
#define R9  13
#define R8  14
#define R7  15
#define R6  16
#define R5  17
#define R4  18
#define R3  19
#define R2  20
#define R1  21

#define NREGFRAME 22 /* words in the trap frame */

/*
 * Semantic aliases used by the C readers.  These are the BESM-6 syscall ABI
 * (doc/Besm6_Calling_Conventions.md, cmd/sim/syscall.cpp), NOT x86 renames:
 * a syscall result is ACC, an error is errno-in-r14 (0 on success) with no
 * carry flag.  R_VAL2's slot is provisional -- task 15d fixes the convention.
 */
#define R_ERRNO R14 /* syscall errno; 0 == success.  There is no carry bit. */
#define R_VAL2  R13 /* TODO 15d: second syscall result (pipe/getpid/fork) */

/*
 * The saved mode word СПСВ carries РежЭ|РежПр (RUU_EXTRACODE|RUU_INTERRUPT,
 * octal 014); both bits clear iff the interrupted context was user mode.  This
 * replaces the x86 CS-ring test.  See doc/Context_Switch.md and doc/Memory_Mapping.md.
 */
#define SPSW_MODE      014 /* РежЭ | РежПр */
#define USERMODE(spsw) (((spsw) & SPSW_MODE) == 0)

/*
 * BASEPRI(): were we interrupted at base priority?  The x86 compared a saved
 * priority level in the frame; the BESM-6 has no per-frame priority -- the
 * current spl is the МГРП mask managed globally in intr.c.  The real test
 * belongs to clock()'s retarget, task 15e; until then this reads "at base
 * priority" so the callout path is never spuriously skipped.  clock() does not
 * fire until 15e wires GRP_TIMER into extintr(), so nothing depends on it yet.
 */
#define BASEPRI(x) (0) /* TODO 15e: test the global spl / МГРП mask */

struct trap {
    int acc;  /*  0  ACC */
    int rreg; /*  1  R */
    int rmr;  /*  2  РМР */
    int iret; /*  3  ИРЕТ (interrupt / fault return) */
    int eret; /*  4  ЭРЕТ (extracode return) */
    int spsw; /*  5  СПСВ */
    int creg; /*  6  М16 = C register M[020] */
    int r15;  /*  7  М15  (SP) */
    int r14;  /*  8  М14  (errno / arg count) */
    int r13;  /*  9  М13 */
    int r12;  /* 10  М12 */
    int r11;  /* 11  М11 */
    int r10;  /* 12  М10 */
    int r9;   /* 13  М9 */
    int r8;   /* 14  М8 */
    int r7;   /* 15  М7 */
    int r6;   /* 16  М6 */
    int r5;   /* 17  М5 */
    int r4;   /* 18  М4 */
    int r3;   /* 19  М3 */
    int r2;   /* 20  М2 */
    int r1;   /* 21  М1 */
};
