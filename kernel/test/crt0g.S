//
// crt0g.s -- forge-entry startup for the STACK-GROWTH test (ugrow), task 17.
//
// The sibling of crt0t.S: the same door (0500, a data-protection fault) and the same gate, but
// the fault means something different.  In utrap the closed page is a dead end and the point is
// that the faulting instruction RE-EXECUTES.  Here the closed page is the page just above the
// user stack, and the point is what the handler does about it: with the BESM-6 stack growing UP
// from 070000, growing it appends a page at the next higher VIRTUAL address and at the END of
// the image at the same time -- so the pages already in the stack keep the addresses they had,
// and grow() has nothing to shuffle.  That is the claim task 17 rests on, and this test is what
// makes it fail if it stops being true.
//
// The pieces:
//   * main() (ugrow.c) programs a user map with sureg(): text at virtual page 0, data at 1,
//     and a ONE-page stack at USTKPAGE = 28 (070000-071777).  Virtual page 29 (072000) is
//     therefore closed -- it is the next stack page, not yet grown.  main() seeds the existing
//     stack page with a sentinel, physically, and remembers where that page lives.
//   * gouser() forges R, Y (РМР), M[16] and r15 and `выпр's into uprog at virtual page 0.
//   * uprog stores a sentinel one word past the stack top (fault 1), then touches virtual
//     page 30 -- which is never opened -- to say "done" (fault 2).
//   * trap() (ugrow.c) backs the PC up, checks the reported page, performs the mapping half of
//     the new grow() (bump u_ssize, clearseg the new tail, sureg) and returns; the store then
//     re-executes into the page that now exists.  On the second fault trap() checks everything
//     and calls halt() with the fault mask.
//
// The forged values, the map and the addresses are shared with ugrow.c through these EQUs; keep
// the two in step.  See doc/Kernel_Assembly_Routines.md section 3 for the gates, and the header
// of crt0t.S for why the gate is duplicated here instead of linking kernel/besm6.o.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gate and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them; ugrow.c checks them in the frame) ----------
RMODE   = 044                   // R: ω = logical + overflow-suppress, distinct from the ABI's 7
RMR     = 0123456               // Y: a distinctive younger-bits value
MODVAL  = 04010                 // M[16]: inert, but the gate must preserve it

// -- the user stack, and the pages either side of its top ------------------------------------
// USTKPAGE = 28, so the one page the process starts with is 070000-071777.  r15 is forged at
// the base, as exec() now seeds it (kernel/sys1.c).
USTK    = 070000                // virtual page 28: the stack base -- the ONE page that exists
USPV    = USTK                  // forged r15, as exec() now seeds it
GROWADR = 072000                // virtual page 29: one word past the stack top -- FAULT 1
DONEADR = 074000                // virtual page 30: never opened -- FAULT 2, the "done" signal
DATADR  = 02000                 // virtual page 1: the data page, where main() seeds SENTNEW

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
// crt0g.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses.  The trailing `.org PGSZ' pads the const section to a full page so
// the .text below -- and with it uprog -- links into a NON-zero physical page: a zero РП
// descriptor is non-executable, so uprog cannot live in physical page 0.

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
        vtm     02003           // PSW := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // ugrow.c: build the map, seed the sentinel, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is exactly where ugrow.ini wants it.  Called from trap() on the second
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
// The forged SPSW is ZERO: mode bits clear (user, mapping and protection ON, interrupts on --
// main() masks МГРП, so there is no source), the modifier NOT armed and SPSW_RIGHT_INSTR clear,
// so `выпр' lands on the LEFT half of uprog's first word.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL -- inert, but the gate must preserve it
        xta                     // forged SPSW := 0 (user mode, nothing armed)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the base of the user stack, as exec seeds it
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// Everything is addressed through r14: uprog must touch no absolute kernel memory, and it must
// not disturb R, Y or M[16] -- `vtm', `xta' and `atx' leave all three alone, which is what lets
// trap() check them in the frame.  The sentinel it plants is READ from the mapped data page
// rather than spelled as a literal: the literal pool lives in the crt0's .const at physical
// page 0, which is NOT what virtual page 0 maps to, so a `#(...)' operand would read garbage.
        .globl  uprog
uprog:
     14 vtm     DATADR          // #1 L: virtual page 1, word 0 -- seeded by main()
     14 xta     0               // #2 R: A := the sentinel
     14 vtm     GROWADR         // #3 L: one word past the stack top
     14 atx     0               // #4 R: FAULT 1 -- page 29 is closed.  trap() grows the stack;
                                //       on the retry this store lands in the page that now is.
     14 vtm     DONEADR         // #5 L: the page that is never opened
     14 xta     0               // #6 R: FAULT 2 -- trap() takes this as "done" and halts
        uj      uprog           // #7 L: (never reached)

// ----------------------------------------------------------------------------
// The fault gate -- a copy of kernel/besm6.S:trapgate, with the shared epilogue (`intret')
// spliced in behind it.  Identical to crt0t.S's; see that file's header for the idioms and
// doc/Dubna_Context_Switch.md §4/§6/§7 for the authority.
// ----------------------------------------------------------------------------
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
        //     in panic, so this door never nests and needs no `SPSW & 014' discriminator. ---
     15 vtm     [ustkbase]      // M15 := kernel stack base = frame base F

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

        // --- pick the door, as kernel/besm6.S does: a supervisor fault branches to ktrap() and
        //     never comes back, a user fault opens the level (grow() is exactly the sleeping path
        //     that wants it) and calls trap().  Nothing is passed either way; the frame is always
        //     at [ustkbase].  ugrow forges user mode, so it must take the second arm -- ugrow.c's
        //     ktrap() stub raises F_KTRAP if it does not. ---
        ita     SPSW            // A := SPSW (live)
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     ktrap           // РежЭ|РежПр set -> the kernel faulted.  A branch, not a call
        vtm     3               // zero -> from user: PSW := БлП|БлЗ, interrupts delivered

     13 vjm     trap            // ugrow.c: check the frame, back up the PC, grow the stack

        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret).  It shuts the door
        //     first: the gate opened the level, and nothing below survives a nested gate. ---
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
// grows up into the reservation.
kstack: . = . + 512
