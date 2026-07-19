//
// crt0c.S -- forge-entry startup for the from-USER-mode TIMER test (uclock).
//
// The fourth sibling of crt0u.S (external interrupt), crt0t.S (fault) and crt0s.S (extracode).
// It is pointed at the same door crt0u.S is -- 0501 -> intrgate -- but where uintr proved the
// GATE, this one proves what is now BEHIND it: the kernel's real extintr() and clock().  Task 15e.
//
// So it is built like crt0s.S rather than crt0u.S: the gate is duplicated (besm6.o still cannot
// enter a standalone test), but the code under test is LINKED, not copied -- kernel/intr.c and
// kernel/clock.c come in whole, and uclock.c supplies only the environment they name.
//
// Two consequences follow from linking the real intr.c, and both shape this file:
//   * extintr() reads and writes `u.u_ar0', so `u' must be the real `struct user' -- reserved
//     here in .bss (in the kernel it is the absolute page at 076000), with `ustkbase' pointed at
//     u.u_stack by main(), exactly as crt0s.S does.  The gate's stack is then inside `u', which
//     is where clock()'s C frames want to be.
//   * intrgate publishes the frame base in `intrframe' for clock() to read, so that cell is
//     reserved here, the way kernel/besm6.S reserves it for the real kernel.
//
// The pieces:
//   * main() (uclock.c) programs a user map with sureg(), seeds the stack sentinel, arms a
//     one-tick callout, opens the interrupt level with the real spl0(), raises GRP_TIMER with
//     `увв 031' and calls gouser().
//   * gouser() forges R, Y (РМР), M[16] and r15 and `выпр's into uprog at virtual page 0.  The
//     tick is already pending, so it fires at uprog's very first instruction -- deterministically,
//     with no dependence on how fast the simulator runs.
//   * uprog immediately reads a CLOSED page; that data fault vectors to 0500 -> toreport, and
//     report() (uclock.c) checks what the tick did and halts with a mask.
//
// The forged values, the map and the sentinel addresses are shared with uclock.c through these
// EQUs; keep the two in step.  See doc/Kernel_Assembly_Routines.md section 3 for the gates.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gate and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them) ---------------------------------------------
RMODE   = 044                   // R: ω = logical (bits 5-3 = 001) + overflow-suppress (bit 6)
RMR     = 0123456               // Y: a distinctive younger-bits value
MODVAL  = 04010                 // M[16]: inert (the forged СПСВ does not arm the modifier)
USPV    = 070010                // r15: the forged user stack pointer.  It is a TRIPWIRE --
                                //   physical 070010 is free memory, uclock.c seeds it, and a gate
                                //   that failed to switch r15 would build the frame right on top.
                                //   That check bites harder here than in uintr: what runs on the
                                //   switched stack is not a two-line stub but extintr(), clock(),
                                //   the callout and the whole proc[] sweep.
KSENT   = 0333333               // uclock.c seeds physical USPV/USPV+1 with this

CLOSEDPG = 010000               // virtual page 4: never mapped -- uprog's way back to supervisor

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
// crt0c.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses (same rule as kernel/besm6.S).  The trailing `.org PGSZ' pads the const
// section to a full page so the .text below -- and with it uprog -- links into a NON-zero physical
// page: a zero РП descriptor is non-executable, so uprog cannot live in physical page 0.
//
// 0500 goes to the test's exit path: uprog finishes by touching a closed page, and a data-protection
// fault IGNORES ПоП and always vectors (doc/Memory_Mapping.md).

        .const
        .org 0500
        uj      toreport        // 0500: internal interrupt -- uprog's deliberate exit fault
      : uj      intrgate        // 0501: external interrupt -- the timer tick
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
// The gate's temp cells, at the very top of .text -- NOT in .bss, where crt0u.S and crt0s.S put
// theirs.  The gate must reach them with a BARE `atx save_a': a `< sym >' escape emits a `мода'
// (utc) that loads M[16], which would overwrite the very C register the gate is saving.  A bare
// address is 12 bits, so the cell has to live below 07777 -- and this image's .bss no longer does,
// because linking the real clock.o and intr.o (and proc[NPROC] with them) pushed it past 010000.
// .text is pinned right after the padded vector page, so putting them here fixes it at ~02000.
// kernel/besm6.S keeps its five in .text for exactly this reason.  Nothing falls into them.
save_a:  .word 0                // A
save_r:  .word 0                // R   -- ALU mode (ω + the NTR suppress bits)
save_y:  .word 0                // Y (РМР) -- younger-bits register
save_c:  .word 0                // M16 -- the C register / address modifier
save_sp: .word 0                // interrupted M15 (the stack pointer)

// The frame base intrgate publishes for clock(); see kernel/besm6.S.
        .globl  intrframe
intrframe: .word 0              // struct trap * -- valid only inside intrgate

        .globl  _start
_start:
     15 vtm     kstack          // main()'s own supervisor stack (the gate uses u.u_stack)
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // uclock.c: build the map, arm the callout, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is exactly where uclock.ini wants it.  Called from report(); never returns.
        .globl  halt
halt:   stop                    // halt with the fault mask in ACC
        uj      halt

// The exit path.  uprog's closing data fault vectors here.  No frame is built and nothing is
// saved: we never go back to user mode, so all this needs is a stack under report().
toreport:
     15 vtm     kstack
     13 vjm     report          // uclock.c: check what the tick did, halt(mask)
        uj      halt0

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// crt0u.S's gouser verbatim.  The forged СПСВ is ZERO: mode bits clear (USER mode -- which is what
// makes clock() charge u_utime and is the sharpest thing this test checks), mapping and protection
// ON, БлПр clear so the pending tick fires immediately, the modifier NOT armed, and
// SPSW_RIGHT_INSTR clear so `выпр' lands on the LEFT half of uprog's first word.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL -- inert, but the gate must preserve it
        xta                     // forged СПСВ := 0 (user mode, interrupts ON)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the user stack / stack-switch tripwire
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode -- the tick fires here

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// There is deliberately almost nothing here.  The tick is already pending when `выпр' clears БлПр,
// so it is taken BEFORE the first instruction below retires; everything this test checks is kernel
// state that report() reads directly, and uintr already covers what the gate hands back to user.
// Keeping uprog to two instructions also keeps the run short, which matters: the interval timer
// free-runs at 250 Hz and cannot be switched off (the SIMH CLK device has no DEV_DISABLE), so the
// fewer instructions between `выпр' and the exit fault, the smaller the chance of a SECOND tick.
// The checks in report() tolerate one anyway -- see the comment there -- but this makes it moot.
//
// uprog must touch no absolute kernel memory and uses NO `#(...)' pool constants: the literal pool
// lives in crt0c.o's .const at physical page 0, which is not what virtual page 0 maps to.
        .globl  uprog
uprog:
     14 vtm     CLOSEDPG
     14 xta     0               // fault -> vector 0500 -> toreport -> report()
        uj      uprog           // (never reached)

// ----------------------------------------------------------------------------
// The external-interrupt gate -- a copy of kernel/besm6.S:intrgate, with its epilogue.
// ----------------------------------------------------------------------------
// Identical in LOGIC to kernel/besm6.S and to crt0u.S's copy; that file's header and
// doc/Context_Switch.md §4/§6/§7/§14 are the authority for every step.  The four saves it exists
// for (R, Y, M[16], the r15 switch) are uintr's subject, not this test's -- what matters here is
// the `atx intrframe' below, which is how clock() learns where the frame is.
//
// The temp cells are addressed BARE (no `< sym >' utc escape): an escape emits a `мода' that loads
// M[16], which would overwrite the very C register the gate is saving.
        .globl  intrgate
intrgate:
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

        // --- stack switch: repoint M15 at the kernel stack ONLY when interrupted from user ---
        ita     SPSW            // СПСВ -> A
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     extk            // РежЭ | РежПр -> nested in the kernel: keep M15
     15 vtm     [ustkbase]      // zero -> from user: M15 := kernel stack base
extk:                           // M15 = frame base F (ustkbase, or the interrupted kernel SP)

        // --- publish the frame base for clock() (kernel/besm6.S does the same) ---
        ita     15              // A := F -- the `aax' result it overwrites is dead here
        atx     intrframe       // a plain cell, bare-addressed: no escape, no M16 touched

        // --- FULSAV: fill the reg.h frame at [M15 ..] (§6); xts/its walk M15 F -> F+21 ---
        xta     save_a
        xts     save_r          // frame[0]  := A     ; A := R
        xts     save_y          // frame[1]  := R     ; A := Y
        its     IRET            // frame[2]  := Y     ; A := IRET (M033, live)
        its     SPSW            // frame[3]  := RET   ; A := СПСВ (M027, live)
        xts     save_c          // frame[4]  := СПСВ  ; A := M16 -- the word USERMODE() reads
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

     13 vjm     extintr         // kernel/intr.c -- THE REAL ONE


        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret) ---
intret:
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
        sti     MOD             // restore C;    A := СПСВ from frame[4]
        sti     SPSW            // restore СПСВ; A := RET from frame[3]
        sti     IRET            // restore RET into М033 (IRET);  A := Y from frame[2]
        stx     save_y          // prepare Y;    A := R from frame[1]
        stx     save_r          // prepare R;    A := A from frame[0]
        atx     save_a          // prepare A
        xta     save_y          // Y back, via the `aex' side effect (§7)
        aex                     //   (A is garbage after this -- intentional)
        xta     save_sp         // interrupted M15 back
        ati     15
        xta     save_a          // A back (xta does not disturb Y)
        xtr     save_r          // R back -- must be last (a later xta would perturb it)
      3 ij                      // выпр: restore the mode word, jump via M[033]

// ----------------------------------------------------------------------------
        .data
// ustkbase holds the base of the stack the gate switches to.  Like crt0s.S, this one is filled at
// RUN TIME by uclock.c with &u.u_stack[0]: the real clock() and extintr() run on it, so it has to
// be inside `u' where there is room for them.
        .globl  ustkbase
ustkbase: .word 0

// uprog's link-time WORD address, as a plain integer -- the linker relocates the symbol into this
// word.  main() reads it to build the map; `(unsigned)&uprog' in C would hand back a fat pointer
// whose word address is not simply its low bits.
        .globl  uprogadr
uprogadr: .word uprog

        .bss
// The u-area.  In the kernel this is the absolute page at 076000; here it is ordinary storage.  It
// must hold `struct user' AND the kernel stack that grows up from u.u_stack -- the 21-word frame,
// then extintr(), clock(), the callout and timeout() -- so two pages, as in crt0s.S.
        .globl  u
u:      . = . + 4096

// main()'s own supervisor stack, separate from the gate's.
kstack: . = . + 512
