//
// crt0u.s -- forge-entry startup for the from-USER-mode interrupt test (uintr).
//
// crt0.s brings the machine up and runs main() in supervisor (kernel) mode, which is enough
// for sctest/mmutest but useless for testing besm6.S's `extint' stack switch: an interrupt
// taken in kernel mode does NOT switch the stack, so the one gap that matters would go
// unexercised.  This crt0 instead forges a real USER-mode context -- a Dubna SELECT-style
// gate (doc/Context_Switch.md §8): forge ИРЕТ + СПСВ and `выпр' into user -- so the coming
// external interrupt lands with СПСВ & 014 == 0 and exercises the full corrected `extint'.
//
// The pieces:
//   * main() (uintr.c) programs a user map with sureg(), arms the console interrupt, polls
//     until its "printing finished" bit is pending, then calls gouser().
//   * gouser() forges the four registers whose preservation is under test (R, Y (РМР), M[16] (M[020]),
//     r15) and `выпр's into the user program uprog at virtual page 0.
//   * The pending console interrupt fires at uprog's first instruction; the hardware vectors
//     to 0501 -> the corrected `extint', which saves the full async machine, switches to the
//     kernel stack, runs extintr(), restores, and `выпр's back into uprog.
//   * uprog reads back R, Y, the modifier-armed word and r15 into r8-r11 and `stop's; in
//     user mode `стоп' is re-dispatched as extracode э63 -> vector 0563 -> report().
//   * report() (supervisor, unmapped) compares r8-r11 against the forged values and the
//     physical stack-switch sentinel, folds the result into the accumulator, and halts.
//
// The forged values, the map and the sentinels are shared with uintr.c through these EQUs and
// the assumptions in the comments; keep the two in step.
//
// This harness (forge-user entry + sureg map) is what tasks 15c/15d reuse for the trap and
// syscall gates.  See kernel/TODO.md task 15a and doc/Context_Switch.md §14.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gate and gouser, matching kernel/besm6.S.  The СПСВ register
// is 027; the forged mode word gouser plants there is SPSW_MOD_RK (from the shared header above).
SPSW = 027                      // saved mode word (М027)
ERET = 032                      // extracode return address (М032)
IRET = 033                      // interrupt / fault return address (М033)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them; report checks them) ----------------------
RMODE   = 044                   // R: ω = logical (bits 5-3 = 001) + overflow-suppress (bit 6).
                                //   Logical so uprog's `yta' returns Y clean; distinct from
                                //   the C ABI's steady R = 7 (logical + round + norm suppress),
                                //   so `rte' catches a dropped R save.
RMR     = 0123456               // Y: a distinctive younger-bits value.
MODVAL  = 04010                 // M[16]: a virtual data address (page 2, offset 8).  The
                                //   armed modifier makes uprog's first `xta 0' read here.
SENT    = 0525252               // the word uintr.c pokes at virtual MODVAL.
USPV    = 074000                // r15: an (unmapped) user-stack value; also the PHYSICAL page
                                //   where extintr()'s frame lands if the switch is missing.
KSENT   = 0333333               // uintr.c seeds physical USPV/USPV+1 with this; extintr() on the
                                //   user r15 would overwrite it.

CLOSEDPG = 024000               // virtual page 10: unmapped, so closed to data -- uprog reads it to
                                //   raise a data-protection fault, which reaches report() below.
// uprog stashes the four read values into these words of its mapped data page (virtual page 2),
// full 48 bits -- NOT into index registers, which are only 15 bits and would truncate SENT/R/Y.
// report reads them back through the same map.  They must clear MODVAL (04010, the sentinel).
VSLOT0  = 04020                 // M[16] read
VSLOT1  = 04021                 // R
VSLOT2  = 04022                 // Y
VSLOT3  = 04023                 // r15

// -- fault bits reported in the accumulator (0 == all five preserved) -----------------------
//   1  M[16] (the modifier-armed read), 2  R, 4  Y, 010 r15 value, 020 stack not switched.

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
// crt0u.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses (same rule as kernel/besm6.S).  The trailing `.org PGSZ' pads the
// const section to a full page so the .text below -- and with it uprog -- links into a NON-zero
// physical page: a zero РП descriptor is non-executable, so uprog cannot live in physical page 0.
//
// 0500 is pointed at report(): uprog finishes by touching a closed page, and a data-protection
// fault IGNORES ПоП and vectors (doc/Memory_Mapping.md, "ПоП is deliberately ignored for
// data-protection faults") -- unlike `стоп' -> extracode э63, which check-halts here because reset
// leaves ПоК set.  So the fault is the clean way back to supervisor for the checker.

        .const
        .org 0500
        uj      report          // 0500: internal interrupt -- uprog's deliberate data fault
      : uj      extint          // 0501: external interrupt (ГРП)
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
        .globl  _start
_start:
     15 vtm     kstack          // seed the kernel stack
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF.
                                //   Reg field 0 in supervisor writes the mode bits from the
                                //   address; keeping БлПр set is what holds interrupts off
                                //   through main, until gouser's `выпр' enters user mode.
     13 vjm     main            // uintr.c: build the map, arm the interrupt, call gouser()
halt:   stop                    // main() should never return (gouser does not)
        uj      halt

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// One parameter (uentry, the virtual entry address of uprog) arrives in the accumulator.
// Runs in supervisor; `выпр' does not return -- control resurfaces in uprog, in user mode.
        .globl  gouser
gouser:
        ati     IRET            // ИРЕТ (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL (inert; `выпр' re-arms it from СПСВ)
        xta   #(SPSW_MOD_RK)    // forged СПСВ: mode bits clear (USER, mapping+interrupts on), the
        ati     SPSW            //   modifier-arm bit set -> `выпр' re-arms M[16].  M[027] := it
     15 vtm     USPV            // r15 := the user-stack value under test
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode, modifier armed

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// It must touch no absolute kernel memory -- every data address it names would go through the
// user map -- so it only reads registers and the one modifier-armed word, stashes the four
// values into r8-r11, and traps out with a data-protection fault.  report() does all the
// comparing, unmapped.
//
// The interrupt is pending at entry, so it fires BEFORE this first instruction; `extint' handles
// it and `выпр's back here with the modifier re-armed.  The first `xta 0' is therefore modified by
// the preserved M[16] and reads virtual MODVAL.
// The four reads must precede any logical op (which would clobber Y) and any ω-change: `xta',
// `rte', `yta', `ita' leave Y alone, and the forged R is already logical so `xta' keeps it; the
// stores go through `atx'/`vtm', which touch neither.  So read, store full-width, in this order.
        .globl  uprog
uprog:
        xta     0               // A := mem[0 + M[16]] = mem[MODVAL]   -- tests M[16]
     14 vtm     VSLOT0
     14 atx     0               //   store it full-width to the mapped data page
        rte     07777           // A := R << 41                          -- tests R
     14 vtm     VSLOT1
     14 atx     0
        yta                     // A := Y (R is logical -> full copy)   -- tests Y
     14 vtm     VSLOT2
     14 atx     0
        ita     017             // A := r15                              -- tests r15 value
     14 vtm     VSLOT3
     14 atx     0
     14 vtm     CLOSEDPG        // trap to supervisor: read a closed page ->
     14 xta     0               //   data-protection fault -> vector 0500 -> report()
        uj      uprog           // (never reached)

// ----------------------------------------------------------------------------
// report -- reached in SUPERVISOR via the 0500 fault vector.  Compare and halt.
// ----------------------------------------------------------------------------
// The four values are in uprog's data page; report reads them back through the still-loaded map
// (a `vtm 02002'/`vtm 02003' bracket toggles БлП while keeping БлПр set, as uarea.s does), and the
// `#(...)' pool constants and the physical switch-sentinel resolve unmapped to real low memory.
// The fault mask is accumulated in r14; a register prefix is DECIMAL, so r14 is `14' here, while
// the operand of `ita 016' that reads it back is OCTAL (016 == M[14]).
report:
     14 vtm     0               // r14 := fault mask
     13 vtm     VSLOT0
        vtm     02002           // map on (БлП off), interrupts still off
     13 xta     0               // A := the stored M[16] read, through the map
        vtm     02003           // map off
        aex   #(SENT)
        uza     rp1             // matches -> M[16] preserved
     14 utm     1
rp1: 13 vtm     VSLOT1
        vtm     02002
     13 xta     0               // A := the stored R value
        vtm     02003
        aex   #(.47|.44)        // RMODE = 044 read by `rte 07777' is bits 47 and 44
        uza     rp2
     14 utm     2
rp2: 13 vtm     VSLOT2
        vtm     02002
     13 xta     0               // A := the stored Y
        vtm     02003
        aex   #(RMR)
        uza     rp3
     14 utm     4
rp3: 13 vtm     VSLOT3
        vtm     02002
     13 xta     0               // A := the stored r15 value
        vtm     02003
        aex   #(USPV)
        uza     rp4
     14 utm     010
rp4:                            // the stack-switch discriminator: was the user r15 written on?
     13 vtm     USPV            // r13 := the physical sentinel address (unmapped)
     13 xta     0               // A := mem[USPV]
        aex   #(KSENT)
        u1a     rpbad           // clobbered -> extintr() ran on the user r15
     13 xta     1               // A := mem[USPV+1]
        aex   #(KSENT)
        uza     rpok            // both intact -> the switch happened
rpbad:  14 utm  020
rpok:   ita     016             // A := the fault mask
        stop                    // halt: the .ini asserts ACC == 0
        uj      report

// ----------------------------------------------------------------------------
// The corrected external-interrupt gate -- a copy of kernel/besm6.S:extint.
// ----------------------------------------------------------------------------
// besm6.o cannot enter a standalone test (its 0500 vector reaches into the C kernel and its
// _start seeds no stack), so the gate is duplicated here, as crt0.s already duplicates the old
// subset gate.  Keep it identical in LOGIC to kernel/besm6.S; that file's header and
// doc/Context_Switch.md §4/§6/§7/§14 are the authority for everything below.
//
// The temp cells are addressed BARE (no `< sym >' utc escape).  This is not a nicety: a `< sym >'
// escape emits a `мода' (utc) that loads M[16], so any escape ahead of the M[16] save below would
// overwrite the very C register this gate is trying to save.  besm6.S is bare for the same reason;
// it works here because this image's bss links low (the cells are under the 12-bit short-address
// field -- save_a is at ~03231, see uintr.nm), just as besm6.S's cells do at ~0700.
//
// Builds the canonical reg.h frame (0 A, 1 R, 2 Y, 3 ИРЕТ, 4 ЭРЕТ, 5 СПСВ, 6 M16, 7 M15 ... 21 M1)
// on the kernel stack with the Dubna FULSAV `xts'/`its' pipeline (§6): M1-M14 and ИРЕТ/ЭРЕТ/СПСВ go
// in live, only A, R, Y, M16 and the interrupted M15 are spilled to the five cells first.  Switches
// M15 to the kernel stack, but ONLY when the interrupt came from user mode (СПСВ & 014 == 0).
// Restores by the inverse stx/sti pop of the whole frame, applying old M15/Y/A/R last in the forced
// order Y -> A -> R (§7).  The register indices SPSW/ERET/IRET/MOD are defined at the top, matching
// kernel/besm6.S.
        .globl  extint
extint:
        // --- short save: the five registers the FULSAV pipeline cannot reach ---
        atx     save_a          // A first, as the vector does
        rte     07777           // R   -> A
        atx     save_r
        yta                     // Y   -> A
        atx     save_y
        ita     MOD             // M16 (C register) -> A -- before the u1a/vtm utc below
        atx     save_c
        ita     15              // interrupted M15 -> A -- before the stack switch repoints it
        atx     save_sp

        // --- stack switch: repoint M15 at the kernel stack ONLY when interrupted from user ---
        ita     SPSW            // СПСВ -> A
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     extk            // РежЭ | РежПр -> nested in the kernel: keep M15
     15 vtm     [ustkbase]      // zero -> from user: M15 := kernel stack base
extk:                           // M15 = frame base F (ustkbase, or the interrupted kernel SP)

        // --- FULSAV: fill the reg.h frame at [M15 ..] (§6); xts/its walk M15 F -> F+22 ---
        xta     save_a
        xts     save_r          // frame[0]  := A     ; A := R
        xts     save_y          // frame[1]  := R     ; A := Y
        its     IRET            // frame[2]  := Y     ; A := ИРЕТ (M033, live)
        its     ERET            // frame[3]  := ИРЕТ  ; A := ЭРЕТ (M032, live)
        its     SPSW            // frame[4]  := ЭРЕТ  ; A := СПСВ (M027, live)
        xts     save_c          // frame[5]  := СПСВ  ; A := M16
        xts     save_sp         // frame[6]  := M16   ; A := interrupted M15
        its     14              // frame[7]  := M15   ; A := M14 (live)
        its     13              // frame[8]  := M14   ; A := M13 (live)
        its     12              // frame[9]  := M13   ; A := M12
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
        its     1               // frame[20] := M2    ; A := M1
        xts     save_a          // frame[21] := M1    ; M15 := F+22 (above the frame); A discarded

     13 vjm     extintr         // uintr.c: dismiss the interrupt

        // --- restore: the exact inverse of the fill.  stx/sti pop frame[21..0], M15 walking
        //     F+22 -> F: M1-M14, M16 and ИРЕТ/ЭРЕТ/СПСВ go straight back to their registers (the
        //     spec-register reloads are no-ops -- extintr() left M027/M032/M033 untouched), while
        //     old M15, Y, R and A are re-stashed to the cells and applied last, in the forced
        //     Y -> A -> R order (§7).  M1-M7 are ABI callee-saved, so their reload is redundant but
        //     keeps the pop symmetric.  A blank-address `stx' stores to word 0, which is discarded. ---
        stx                     // A := M1 from frame[21]
        sti     1               // restore M1; A := M2 from frame[20]
        sti     2
        sti     3
        sti     4
        sti     5
        sti     6
        sti     7
        sti     8
        sti     9
        sti     10
        sti     11              // restore M11;  A := M12 from frame[10]
        sti     12              // restore M12;  A := M13 from frame[9]
        sti     13              // restore M13;  A := M14 from frame[8]
        sti     14              // restore M14;  A := M15 from frame[7]
        stx     save_sp         // prepare M15;  A := M16 from frame[6]
        sti     MOD             // restore C;    A := СПСВ from frame[5]
        sti     SPSW            // restore СПСВ; A := ЭРЕТ from frame[4]
        sti     ERET            // restore ЭРЕТ; A := ИРЕТ from frame[3]
        sti     IRET            // restore ИРЕТ; A := Y from frame[2]
        stx     save_y          // prepare Y;    A := R from frame[1]
        stx     save_r          // prepare R;    A := A from frame[0]
        atx     save_a          // prepare A
        xta     save_y          // Y back, via the `aex' side effect (§7)
        aex                     //   (A is garbage after this -- intentional)
        xta     save_sp         // interrupted M15 back (M15 no longer names the frame -- fine)
        ati     15
        xta     save_a          // A back (xta does not disturb Y)
        xtr     save_r          // R back -- must be last (a later xta would perturb it)
      3 ij                      // выпр: restore the mode word, jump via M[033]

// ----------------------------------------------------------------------------
        .data
// ustkbase holds the kernel stack base for extint's switch, exactly as machdep.c does for the
// real kernel.  [ustkbase] loads its CONTENTS (the address of kstack).
        .globl  ustkbase
ustkbase: .word kstack

// uprog's link-time WORD address, as a plain integer -- the linker relocates the symbol into
// this word.  main() reads it to build the map; going through `(unsigned)&uprog' in C instead
// would hand back a fat pointer whose word address is not simply its low bits.
        .globl  uprogadr
uprogadr: .word uprog

        .bss
// The extint temp cells (see the gate above).  In .bss, but it links low so the gate reaches
// them with a bare `atx save_a' -- an escape would clobber M[16] mid-save.
save_a:  . = . + 1
save_r:  . = . + 1
save_y:  . = . + 1
save_c:  . = . + 1
save_sp: . = . + 1

// The kernel stack _start seeds and extint switches to.  The label is the low end; the stack
// grows up into the reservation.  Well clear of USPV = 074000.
kstack: . = . + 256
