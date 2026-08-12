//
// crt0f.s -- forge-entry startup for the ARITHMETIC-fault test (ufpe).
//
// The sibling of crt0t.S, pointed at the same door (0500 -> trapgate) but at the two causes
// crt0t.S cannot raise: a floating overflow and a floating divide by zero.  They differ from
// the protection faults in one visible way, and that difference is what this test exists for:
// the machine vectors with the PC already past the faulting instruction AND WITHOUT setting
// SPSW_NEXT_RK, so trap()'s restart fix-up is a no-op and a returning handler resumes AFTER
// the fault rather than on it.
//
// Unlike utrap, this test links the REAL kernel/trap.c (ufpe.c supplies the neighbours it
// calls), so the frame lands at u.u_stack: `u' is reserved here with a page of slack above it
// and ufpe.c's main() points `ustkbase' at u.u_stack before entering user mode.
//
// The forged values, the map and the slot addresses are shared with ufpe.c through these EQUs;
// keep the two in step.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gate and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values ------------------------------------------------------------------
RMODE   = 7                     // R: the C ABI's steady value -- ω = logical, norm and round
                                //   suppressed, and OVERFLOW NOT SUPPRESSED (bit 6 clear).
                                //   crt0t.S forges 044, which has bit 6 SET; with that value
                                //   the two faults below could not happen at all.
MODVAL  = 04010                 // M[16]: inert, but the gate must preserve it
USPV    = 074000                // r15: an (unmapped) user-stack value

// -- what uprog reads and writes, all in virtual page 2 (mapped from the start) --------------
// EVERY WORD uprog TOUCHES IS HERE, and that is not tidiness: an `xta #(...)' literal goes to
// the CONST segment, which this map does not reach -- virtual page 0 is the TEXT page.  So the
// two marks are seeded by main() and loaded, not assembled inline.
CBASE   = 04000                 // +0 the big constant, +1 zero, +2/+3 the marks uprog stores,
                                //   +4/+5 the values it stores there
FAULT3  = 014000                // virtual page 6: closed, and the "done" signal

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
        .const
        .org 0500
        uj      trapgate        // 0500: internal interrupt -- uprog's deliberate faults
      : uj      badintr         // 0501: external interrupt -- none is expected here
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
        .globl  _start
_start:
     15 vtm     kstack          // seed the kernel stack (main runs on it; the gate switches
                                //   to u.u_stack instead)
        vtm     02003           // PSW := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // ufpe.c: build the map, seed the constants, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is where ufpe.ini wants it.
        .globl  halt
halt:   stop                    // halt with the fault mask in ACC
        uj      halt

// An external interrupt would mean МГРП was left unmasked; halt distinguishably.
badintr: stop   01
        uj      badintr

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// Identical in shape to crt0t.S's, and the same forged SPSW of zero: user mode, mapping and
// protection on, nothing armed, SPSW_RIGHT_INSTR clear -- so `выпр' lands on the LEFT half of
// uprog's first word, which is where the half-word counting below starts.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL
        xta                     // forged SPSW := 0 (user mode, nothing armed)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the user-stack value
        ntr     RMODE           // R := 7 -- overflow NOT suppressed
      3 ij                      // выпр: into uprog, user mode

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// Three faults, and the HALF-WORD each faults from is part of the test: fault 1 comes from a
// RIGHT half and fault 2 from a LEFT one, so both resume arms are exercised.  Each is followed
// by a store that only executes if the resume landed PAST the faulting instruction -- the whole
// difference between these causes and utrap's.
        .globl  uprog
uprog:
        ntr     0               // #1  L: R := 0 -- normalize, round, and FAULT on overflow
     14 vtm     CBASE           // #2  R: r14 -> the constants
     14 xta     0               // #3  L: A := 1e18
     14 a*x     0               // #4  R: FAULT 1 -- overflow, from the RIGHT half
     14 xta     4               // #5  L: reached only by resuming past the multiply
     14 atx     2               // #6  R: ... and this records that it was
     14 xta     0               // #7  L: A := 1e18
     14 utm     0               // #8  R: no-op -- flips the half-word parity for fault 2
     14 a/x     1               // #9  L: FAULT 2 -- divide by zero, from the LEFT half
     14 xta     5               // #10 R
     14 atx     3               // #11 L
     14 vtm     FAULT3          // #12 R: a closed page
     14 xta     0               // #13 L: FAULT 3 -- a DATA fault, so SIGSEGV and not SIGFPE
        uj      uprog           // #14 R: (never reached)

// ----------------------------------------------------------------------------
// The fault gate -- a copy of kernel/besm6.S:trapgate with `intret' spliced in behind it,
// character for character as crt0t.S carries it.  Keep all three in step.
// ----------------------------------------------------------------------------
        .globl  trapgate
trapgate:
        // --- short save: the five registers the FULSAV pipeline cannot reach ---
        atx     save_a          // A first, as the vector does
        rte     07777           // R   -> A
        atx     save_r
        yta                     // Y -> A -- a full copy only because `rte' left ω = logical
        atx     save_y
        ita     MOD             // M16 (C register) -> A -- must precede the stack switch,
        atx     save_c          //   whose `[ustkbase]' escape is a `wtc' that loads M16
        ita     15              // interrupted M15 -> A -- before the stack switch repoints it
        atx     save_sp

        // --- stack switch: UNCONDITIONAL, as in the kernel. ---
     15 vtm     [ustkbase]      // M15 := u.u_stack = frame base F

        // --- FULSAV: fill the reg.h frame at [M15 ..]; xts/its walk M15 F -> F+21 ---
        xta     save_a
        xts     save_r          // frame[0]  := A     ; A := R
        xts     save_y          // frame[1]  := R     ; A := Y
        its     IRET            // frame[2]  := Y     ; A := IRET (M033, live)
        its     SPSW            // frame[3]  := RET   ; A := SPSW (M027, live)
        xts     save_c          // frame[4]  := SPSW  ; A := M16
        xts     save_sp         // frame[5]  := M16   ; A := interrupted M15
        its     14              // frame[6]  := M15   ; A := M14 (live)
        its     13              // frame[7]  := M14   ; A := M13 (live)
        its     12
        its     11
        its     10
        its     9
        its     8
        its     7
        its     6
        its     5
        its     4
        its     3
        its     2
        its     1               // frame[19] := M2    ; A := M1
        xts     save_a          // frame[20] := M1    ; M15 := F+21 (above the frame)

        // --- pick the door, as kernel/besm6.S does. ---
        ita     SPSW            // A := SPSW (live)
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     ktrap           // РежЭ|РежПр set -> the kernel faulted.  A branch, not a call
        vtm     3               // zero -> from user: PSW := БлП|БлЗ, interrupts delivered

     13 vjm     trap            // THE REAL kernel/trap.c

        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret). ---
intret:
        vtm     02003           // PSW := БлП|БлЗ|БлПр: external interrupts blocked

        stx                     // A := M1 from frame[20]
        sti     1               // restore M1; A := M2 from frame[19]
        sti     2
        sti     3
        sti     4
        sti     5
        sti     6
        sti     7
        sti     8
        sti     9
        sti     10
        sti     11
        sti     12
        sti     13
        sti     14              // restore M14;  A := M15 from frame[6]
        stx     save_sp         // prepare M15;  A := M16 from frame[5]
        sti     MOD             // restore C;    A := SPSW from frame[4]
        sti     SPSW            // restore SPSW; A := RET from frame[3]
        sti     IRET            // restore RET into М033 (IRET);  A := Y from frame[2]
        stx     save_y          // prepare Y;    A := R from frame[1]
        stx     save_r          // prepare R;    A := A from frame[0]
        atx     save_a          // prepare A
        xta     save_y          // Y back, via the `aex' side effect
        aex                     //   (A is garbage after this -- intentional)
        xta     save_sp         // interrupted M15 back
        ati     15
        xta     save_a          // A back (xta does not disturb Y)
        xtr     save_r          // R back -- must be last
      3 ij                      // выпр: restore the mode word, jump via M[033]

// ----------------------------------------------------------------------------
        .data
// The gate's stack base.  ZERO here and filled in by main(), because the real trap.c reads the
// frame from u.u_stack and only C knows that member's offset.
        .globl  ustkbase
ustkbase: .word 0

// uprog's link-time WORD address, as a plain integer.
        .globl  uprogadr
uprogadr: .word uprog

        .bss
// The gate's temp cells.  In .bss, but it links low so the gate reaches them with a bare
// `atx save_a' -- an escape would clobber M[16] mid-save.
save_a:  . = . + 1
save_r:  . = . + 1
save_y:  . = . + 1
save_c:  . = . + 1
save_sp: . = . + 1

// `u', with room above it.  In the kernel this is two fixed pages at UBASE whose tail IS the
// kernel stack; here it must be reserved rather than declared, because u_stack is the LAST
// member of struct user and the frame plus trap()'s C frames grow up out of the object.
        .globl  u
u:      . = . + 04000

// The stack _start seeds for main().  The gate switches away from it to u.u_stack.
kstack: . = . + 512
