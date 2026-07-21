//
// crt0t.s -- forge-entry startup for the from-USER-mode FAULT test (utrap).
//
// The sibling of crt0u.s: same Dubna SELECT-style forge into user mode, but pointed at the
// other asynchronous door.  Where uintr takes an EXTERNAL interrupt (0501 -> intrgate), this
// test takes an INTERNAL one -- a data-protection fault on a closed page (0500 -> trapgate)
// -- which is the door task 15c builds.
//
// Two things are under test that uintr cannot see:
//   * The RESTART PROTOCOL.  The machine vectors with the PC already advanced PAST the
//     faulting instruction (doc/Memory_Mapping.md).  trap() backs it up; if it does not, the
//     retry skips the instruction.  uprog faults twice, once from the RIGHT half of a word and
//     once from the LEFT, so both arms of the fixup are exercised -- and each faulting `xta'
//     stores what it read, so a skipped instruction leaves a wrong word behind.
//   * The frame itself.  trap() now receives a `struct trap *' aliasing the gate's frame, so
//     utrap.c checks the interrupted machine (R, Y, M[16], r15) by reading the FRAME, not by
//     having the user read its own registers back.  Because it also clobbers R/Y/M[16] before
//     returning, the next fault's frame proves the epilogue restored them.
//
// The pieces:
//   * main() (utrap.c) programs a user map with sureg() -- text at virtual pages 0-1, data at
//     2-3, so virtual pages 4, 5 and 6 are CLOSED -- and seeds the physical pages behind 4 and
//     5 with sentinels.
//   * gouser() forges R, Y (РМР), M[16] and r15 and `выпр's into uprog at virtual page 0.
//   * uprog reads virtual page 4 (fault 1), then page 5 (fault 2), then page 6 (fault 3).
//   * trap() (utrap.c) checks the frame, backs the PC up, opens the page that faulted and
//     returns; the gate's epilogue resumes uprog, which RE-EXECUTES the faulting `xta' and this
//     time reads the sentinel.  On the third fault trap() calls halt() with the fault mask.
//
// The forged values, the map and the slot addresses are shared with utrap.c through these EQUs;
// keep the two in step.  See doc/Kernel_Assembly_Routines.md section 3 for the gates.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gate and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them; utrap.c checks them in the frame) ----------
RMODE   = 044                   // R: ω = logical (bits 5-3 = 001) + overflow-suppress (bit 6).
                                //   Distinct from the C ABI's steady R = 7, so a dropped R save
                                //   or restore shows up.  Logical also makes the gate's `yta'
                                //   return Y as a full copy.
RMR     = 0123456               // Y: a distinctive younger-bits value.
MODVAL  = 04010                 // M[16]: an inert value (the forged СПСВ does NOT arm the
                                //   modifier here -- uprog names its own addresses through r14).
USPV    = 074000                // r15: an (unmapped) user-stack value; also the PHYSICAL page
                                //   where trap()'s frame would land if the gate failed to switch.
KSENT   = 0333333               // utrap.c seeds physical USPV/USPV+1 with this.

// -- the closed pages uprog walks into, and the slots it reports through --------------------
FAULT1  = 010000                // virtual page 4: closed at entry, opened after fault 1
FAULT2  = 012000                // virtual page 5: closed until fault 2
FAULT3  = 014000                // virtual page 6: never opened -- the "done" signal
VSLOT1  = 04000                 // virtual page 2 (mapped from the start): what the retry of
VSLOT2  = 04001                 //   fault 1 / fault 2 read.  utrap.c reads them back physically.

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
// crt0t.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses (same rule as kernel/besm6.S).  The trailing `.org PGSZ' pads the
// const section to a full page so the .text below -- and with it uprog -- links into a NON-zero
// physical page: a zero РП descriptor is non-executable, so uprog cannot live in physical page 0.
//
// A data-protection fault IGNORES ПоП and always vectors (doc/Memory_Mapping.md), which is what
// makes it usable as the test's trap-out; reset leaves ПоК set, so `стоп' from user would
// check-halt instead.

        .const
        .org 0500
        uj      trapgate        // 0500: internal interrupt -- uprog's deliberate data faults
      : uj      badintr         // 0501: external interrupt -- none is expected here
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
        .globl  _start
_start:
     15 vtm     kstack          // seed the kernel stack (the gate switches back to it)
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // utrap.c: build the map, seed the sentinels, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is exactly where utrap.ini wants it.  Called from trap() on the third
// fault; never returns.
        .globl  halt
halt:   stop                    // halt with the fault mask in ACC
        uj      halt

// An external interrupt would mean МГРП was left unmasked; halt distinguishably.
badintr: stop   01
        uj      badintr

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// One parameter (uentry, the virtual entry address of uprog) arrives in the accumulator.
// Runs in supervisor; `выпр' does not return -- control resurfaces in uprog, in user mode.
//
// The forged СПСВ is ZERO: mode bits clear (user, mapping and protection ON, interrupts on --
// main() masks МГРП, so there is no source), the modifier NOT armed and SPSW_RIGHT_INSTR clear,
// so `выпр' lands on the LEFT half of uprog's first word.  That last bit matters: uprog's
// half-word layout below is counted from there.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL -- inert, but the gate must preserve it
        xta                     // forged СПСВ := 0 (user mode, nothing armed)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the user-stack value under test
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// Three faults, and the HALF-WORD each faults from is part of the test: the restart fixup has
// one arm for each (doc/Memory_Mapping.md, "The restart protocol").  A label aligns to a word
// boundary and the assembler packs two instructions per word, left half first, so the parity
// below is counted from `uprog:' -- the `14 utm 0' at #5 is a no-op whose only job is to flip it.
// Do NOT insert a `:' alignment here instead: the text-segment filler is `utc 0', which would
// load M[16] and destroy the very value the frame check is watching.
//
// Everything is addressed through r14: uprog must touch no absolute kernel memory (every data
// address it names goes through the user map), and it must not disturb R, Y or M[16] -- `vtm',
// `utm', `xta' and `atx' leave all three alone.
        .globl  uprog
uprog:
     14 vtm     FAULT1          // #1 L: point at the first closed page
     14 xta     0               // #2 R: FAULT 1, from the RIGHT half of the word
     14 vtm     VSLOT1          // #3 L: (on the retry) A holds the page-4 sentinel
     14 atx     0               // #4 R: report what the re-executed `xta' read
     14 utm     0               // #5 L: no-op -- flips the half-word parity for fault 2
     14 vtm     FAULT2          // #6 R: point at the second closed page
     14 xta     0               // #7 L: FAULT 2, from the LEFT half of the word
     14 vtm     VSLOT2          // #8 R
     14 atx     0               // #9 L: report what the re-executed `xta' read
     14 vtm     FAULT3          // #10 R: the page that is never opened
     14 xta     0               // #11 L: FAULT 3 -- trap() takes this as "done" and halts
        uj      uprog           // #12 R: (never reached)

// ----------------------------------------------------------------------------
// The fault gate -- a copy of kernel/besm6.S:trapgate, with the shared epilogue (`intret',
// which lives in intrgate over there) spliced in behind it.
// ----------------------------------------------------------------------------
// besm6.o cannot enter a standalone test (its vectors reach into the C kernel and its _start
// seeds no stack), so the gate is duplicated here, exactly as crt0u.S duplicates intrgate.  Keep
// it identical in LOGIC to kernel/besm6.S; that file's headers and doc/Dubna_Context_Switch.md
// §4/§6/§7 are the authority for the idioms, and doc/Unix_Context_Switch.md §1/§5 for the
// registers the hardware does not save.
//
// The temp cells are addressed BARE (no `< sym >' utc escape): an escape emits a `мода' that
// loads M[16], which would overwrite the very C register the gate is saving.
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

        // --- stack switch: UNCONDITIONAL.  A fault from supervisor is a kernel bug that ends
        //     in panic, so this door never nests and needs no `СПСВ & 014' discriminator. ---
     15 vtm     [ustkbase]      // M15 := kernel stack base = frame base F

        // --- FULSAV: fill the reg.h frame at [M15 ..]; xts/its walk M15 F -> F+21 ---
        xta     save_a
        xts     save_r          // frame[0]  := A     ; A := R
        xts     save_y          // frame[1]  := R     ; A := Y
        its     IRET            // frame[2]  := Y     ; A := IRET (M033, live)
        its     SPSW            // frame[3]  := RET   ; A := СПСВ (M027, live)
        xts     save_c          // frame[4]  := СПСВ  ; A := M16
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

        // --- call trap(): nothing is passed.  The switch above is unconditional, so the frame
        //     is always at [ustkbase] and trap() reads that for itself.  A callee with no
        //     parameters returns M15 unchanged, so it comes back at F+21 -- the epilogue's
        //     precondition, with nothing to fix up. ---

        // --- open the interrupt level for the C call, as kernel/besm6.S does ---
        ita     021
        aax     #075777         // clear БлПр: interrupts delivered
        ati     021

     13 vjm     trap            // utrap.c: check the frame, back up the PC, open the page

        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret).  stx/sti pop
        //     frame[20..0], M15 walking F+21 -> F; old M15, Y, R and A are re-stashed to the
        //     cells and applied last, in the forced order Y -> A -> R (§7).  It shuts the door
        //     first: the gate opened the level, and nothing below survives a nested gate. ---
intret:
        ita     021             // A := ПСВ
        aox     #02000          //   set БлПр: external interrupts blocked
        ati     021

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
// ustkbase holds the kernel stack base for the gate's switch, exactly as machdep.c does for the
// real kernel.  [ustkbase] loads its CONTENTS (the address of kstack).
        .globl  ustkbase
ustkbase: .word kstack

// uprog's link-time WORD address, as a plain integer -- the linker relocates the symbol into
// this word.  main() reads it to build the map; `(unsigned)&uprog' in C would hand back a fat
// pointer whose word address is not simply its low bits.
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

// The kernel stack _start seeds and the gate switches to.  The label is the low end; the stack
// grows up into the reservation.  Well clear of USPV = 074000.
kstack: . = . + 512
