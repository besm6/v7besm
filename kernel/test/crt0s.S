//
// crt0s.S -- forge-entry startup for the from-USER-mode SYSCALL test (usys).
//
// The third sibling of crt0u.S (external interrupt, 0501) and crt0t.S (fault, 0500).  This one
// is pointed at the EXTRACODE door: `$77 N' -> 0577 -> sysgate, and `$50' -> 0550 -> badext.
// That is the door task 15d builds.
//
// What is under test, and why it needs a machine rather than b6sim:
//   * THE GATE.  sysgate is trapgate with one instruction changed -- `its ERET' (М032) where the
//     fault gate reads `its IRET' (М033) -- and it leaves through the SAME `intret' epilogue,
//     which reloads that slot into IRET and returns with a single `3 ij'.  If the normalisation
//     is wrong the user does not come back at all.
//   * THE MARSHALLING.  The BESM-6 convention passes argument k of n at r15-(n-k) with the last
//     in the accumulator; the ~35 v7 callees read u.u_ap as an ascending array.  kernel/syscall.c
//     inverts it, and leg 2 below checks that it did.
//   * THE STACK BALANCE.  The gate stands in for a called function and owes the caller the
//     callee's pop of the n-1 words it pushed.  Leg 2 checks r15 came back popped by exactly 2.
//   * badext.  An extracode we do not implement must signal, not halt.
//
// UNLIKE crt0u.S and crt0t.S, THIS ONE LINKS THE REAL DISPATCHER.  kernel/syscall.c is its own
// file precisely so a standalone test can link it (it cannot link trap.c, which drags in the whole
// kernel), so what runs here is the kernel's actual syscall(), not a copy.  Only the gate is
// duplicated, for the standing reason that besm6.o cannot enter a standalone test.
//
// That has one consequence worth stating: syscall() finds its frame at `u.u_stack', so the gate's
// stack MUST be inside `u'.  `u' is therefore reserved here in .bss (in the kernel it is the
// absolute page at 076000), and usys.c points `ustkbase' at u.u_stack before entering user mode.
//
// The pieces:
//   * main() (usys.c) programs a user map with sureg(), seeds the sentinels, points ustkbase at
//     u.u_stack, and calls gouser().
//   * gouser() forges R, Y (РМР), M[16], r13 and r15 and `выпр's into uprog at virtual page 0.
//   * uprog runs four legs -- a 0-argument call, a 3-argument call, an out-of-range number and an
//     unimplemented extracode -- storing the machine state it comes back with after each.
//   * uprog then reads a CLOSED page; the resulting data fault vectors to 0500 -> toreport, and
//     report() (usys.c) checks the stored words and halts with a mask.
//
// The forged values, the map and the slot addresses are shared with usys.c through these EQUs;
// keep the two in step.  See doc/Kernel_Assembly_Routines.md section 3 for the gates.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)

// Machine-register indices for the gates and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
ERET = 032                      // extracode return address (М032)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them; usys.c checks what came back) --------------
RMODE   = 044                   // R: ω = logical (bits 5-3 = 001) + overflow-suppress (bit 6)
RMR     = 0123456               // Y: a distinctive younger-bits value
MODVAL  = 04010                 // M[16]: inert (the forged СПСВ does not arm the modifier)
R13V    = 054321                // r13: the caller's return address.  THE SYSCALL MUST NOT TOUCH
                                //   IT -- that is why the second result lives in r12 and not
                                //   here (R_VAL2, include/sys/reg.h).
USPV    = 070010                // r15: the user stack.  Virtual page 28 (USTKPAGE), so it is
                                //   MAPPED and the program can stage arguments below it -- unlike
                                //   utrap's bare tripwire.  It is still a tripwire too: physical
                                //   070010 is free memory, and usys.c seeds it, so a gate that
                                //   failed to switch r15 would build its frame right on top.
KSENT   = 0333333               // usys.c seeds physical USPV/USPV+1 with this

CLOSEDPG = 010000               // virtual page 4: never mapped -- uprog's way back to supervisor

// -- the arguments leg 2 stages, and a sentinel for leg 4 -----------------------------------
ARG1    = 01111
ARG2    = 02222
ARG3    = 03333
SENTA   = 07654                 // accumulator value badext must leave alone

// -- report slots, in virtual page 2 (mapped from the start; usys.c reads them physically) --
VS_ACC1 = 04000                 // leg 1: accumulator  (r_val1)
VS_ERR1 = 04001                 //        r14          (errno)
VS_VAL2 = 04002                 //        r12          (r_val2)
VS_R13  = 04003                 //        r13          (must be untouched)
VS_R15A = 04004                 //        r15          (must be unchanged: no arguments pushed)
VS_ACC2 = 04005                 // leg 2: accumulator
VS_ERR2 = 04006                 //        r14
VS_R15B = 04007                 //        r15          (must be USPV - 2)
VS_ACC3 = 04010                 // leg 3: accumulator  (must be -1)
VS_ERR3 = 04011                 //        r14          (must be EINVAL)
VS_ACC4 = 04012                 // leg 4: accumulator  (must still be SENTA)
VS_R15C = 04013                 //        r15

// ----------------------------------------------------------------------------
// Interrupt and extracode vectors
// ----------------------------------------------------------------------------
// crt0s.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses (same rule as kernel/besm6.S).  The trailing `.org PGSZ' pads the
// const section to a full page so the .text below -- and with it uprog -- links into a NON-zero
// physical page: a zero РП descriptor is non-executable, so uprog cannot live in physical page 0.
//
// 0500 goes to the test's exit path, not to a gate: uprog finishes by touching a closed page, and
// a data-protection fault IGNORES ПоП and always vectors (doc/Memory_Mapping.md).  That is the
// clean way back to supervisor -- `стоп' from user is re-dispatched as extracode э63 and would
// land in badext instead.

        .const
        .org 0500
        uj      toreport        // 0500: internal interrupt -- uprog's deliberate exit fault
      : uj      badintr         // 0501: external interrupt -- none is expected here
        .org 0550
        uj      badext          // 0550: extracode э50 -- leg 4
        .org 0577
        uj      sysgate         // 0577: extracode э77 -- the syscall gate
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
        .globl  _start
_start:
     15 vtm     kstack          // main()'s own supervisor stack (the gate uses u.u_stack)
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // usys.c: build the map, seed the sentinels, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is exactly where usys.ini wants it.  Called from report(); never returns.
        .globl  halt
halt:   stop                    // halt with the fault mask in ACC
        uj      halt

// An external interrupt would mean МГРП was left unmasked; halt distinguishably.
badintr: stop   01
        uj      badintr

// The exit path.  uprog's closing data fault vectors here.  No frame is built and nothing is
// saved: we never go back to user mode, so all this needs is a stack under report().
toreport:
     15 vtm     kstack
     13 vtm     halt0           // tail call: report() halts, so the `uj' is only a safety net
        uj      report          // usys.c: check the stored words, halt(mask)

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// crt0t.S's gouser with one addition: r13 is forged too, because this door is the one that could
// plausibly clobber it (v7 returned its second value in a register, and r13 was the obvious slot
// until 15d settled on r12).
//
// The forged СПСВ is ZERO: mode bits clear (user, mapping and protection ON, interrupts on --
// main() masks МГРП, so there is no source), the modifier NOT armed and SPSW_RIGHT_INSTR clear,
// so `выпр' lands on the LEFT half of uprog's first word.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL -- inert, but the gate must preserve it
        xta                     // forged СПСВ := 0 (user mode, nothing armed)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the user stack (mapped: leg 2 pushes below it)
     13 vtm     R13V            // r13 := the caller's return address (gouser does not return)
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// uprog must touch no absolute kernel memory: every address it names goes through the user map.
// In particular it uses NO `#(...)' pool constants -- the literal pool lives in crt0s.o's .const,
// at physical page 0, which is NOT what virtual page 0 maps to here.  Constants therefore arrive
// through `vtm' (a 15-bit immediate into an index register) and `ita'.
//
// r9 is the constant courier and r10 the store pointer; both are caller-saved scratch and neither
// is checked.  Everything the test cares about -- the accumulator, r12, r13, r14, r15 -- is read
// out with `ita' and stored full-width to virtual page 2.  NOT into index registers: those are
// 15 bits and would truncate.
//
// AN EXTRACODE ALWAYS RETURNS TO THE **LEFT HALF OF THE NEXT WORD**, and the `10 utm 0' no-ops
// below exist only to live with that.  ERET is a WORD address: the machine stores PC+1 and saves
// no right-instruction indicator (unlike a fault, which records SPSW_RIGHT_INSTR), so that is
// where `выпр' lands whichever half the extracode itself was in -- exactly as `vjm' behaves.
//
// The extracode may sit in EITHER half; what it costs is whatever is packed AFTER it in the same
// word.  From a left half that instruction is simply never executed -- put `$77' on the left here
// and the store that follows it is silently skipped, which is not a kernel bug and no gate can
// undo, because the half is not recorded anywhere.  From a right half nothing is lost, so these
// no-ops push each extracode there; that is a convention for keeping the test readable, not a
// hardware rule.  (b6sim does NOT model any of this -- see kernel/TODO.md.)
//
// A `:' alignment is not usable instead: the text filler is `utc 0', which loads M[16].
        .globl  uprog
uprog:
        // --- leg 1: a syscall with NO arguments (sysent[20], getpid-shaped) ---------------
     10 utm     0               // no-op: puts the extracode in the right half (see above)
        $77     20              // r14 := 20 (the effective address) -> sysent[20]
     10 vtm     VS_ACC1
     10 atx     0               // the accumulator: r_val1
        ita     016
     10 vtm     VS_ERR1
     10 atx     0               // r14: errno, 0 on success
        ita     014
     10 vtm     VS_VAL2
     10 atx     0               // r12: r_val2, the second result
        ita     015
     10 vtm     VS_R13
     10 atx     0               // r13: must still be R13V
        ita     017
     10 vtm     VS_R15A
     10 atx     0               // r15: nothing was pushed, so it must be USPV

        // --- leg 2: a syscall with THREE arguments (sysent[3], read-shaped) --------------
        // Stage them the real way: arguments 1..n-1 descending below r15, the last in the
        // accumulator.  syscall() must hand the callee u_arg[0..2] = ARG1, ARG2, ARG3 -- the
        // ASCENDING prototype order -- and then pop the two pushed words.
      9 vtm     ARG1
        ita     011
     15 atx     -2              // mem[r15-2] := ARG1  (argument 1)
      9 vtm     ARG2
        ita     011
     15 atx     -1              // mem[r15-1] := ARG2  (argument 2)
      9 vtm     ARG3
        ita     011             // A := ARG3           (argument 3, the last one)
     10 utm     0               // no-op: keeps the extracode in the right half
        $77     3
     10 vtm     VS_ACC2
     10 atx     0
        ita     016
     10 vtm     VS_ERR2
     10 atx     0
        ita     017
     10 vtm     VS_R15B
     10 atx     0               // r15: must have come back popped by 2

        // --- leg 3: a syscall number OUTSIDE sysent[] ------------------------------------
        // 100 >= NSYSENT.  syscall() must RANGE-CHECK rather than mask: a mask (the old
        // `& 077') would fold this onto syscall 36 and run it.
     10 utm     0               // no-op: keeps the extracode in the right half
        $77     100
     10 vtm     VS_ACC3
     10 atx     0               // must be -1
        ita     016
     10 vtm     VS_ERR3
     10 atx     0               // must be EINVAL, from nosys()

        // --- leg 4: an extracode we do not implement -------------------------------------
        // э50 -> 0550 -> badext -> badextr(), which signals SIGINS and returns.  The machine
        // must come back untouched and the program must keep running.
      9 vtm     SENTA
        ita     011             // A := SENTA
        $50     0
     10 vtm     VS_ACC4
     10 atx     0               // the accumulator must still be SENTA
        ita     017
     10 vtm     VS_R15C
     10 atx     0               // r15 must not have moved

        // --- done: back to supervisor through a data-protection fault --------------------
     14 vtm     CLOSEDPG
     14 xta     0               // fault -> vector 0500 -> toreport -> report()
        uj      uprog           // (never reached)

// ----------------------------------------------------------------------------
// The extracode gates -- copies of kernel/besm6.S:sysgate and :badext, with the shared epilogue
// (`intret', which lives in intrgate over there) spliced in behind them.
// ----------------------------------------------------------------------------
// besm6.o cannot enter a standalone test, so the gates are duplicated here, exactly as crt0t.S
// duplicates trapgate.  Keep them identical in LOGIC to kernel/besm6.S; that file's headers and
// doc/Dubna_Context_Switch.md §4/§6/§7/§8/§9 are the authority for the idioms, and
// doc/Unix_Context_Switch.md §1/§5 for the four registers the hardware does not save.
//
// The ONE difference from trapgate is `its ERET' below: the hardware puts an extracode's return
// address in М032, not М033.  Framing it there means `intret' needs no change at all -- it
// reloads the slot into IRET and leaves through the same `3 ij', because the `ij' index field
// only picks WHICH REGISTER holds the PC while the mode comes from СПСВ either way.
//
// The temp cells are addressed BARE (no `< sym >' utc escape): an escape emits a `мода' that
// loads M[16], which would overwrite the very C register the gate is saving.
        .globl  sysgate
sysgate:
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

        // --- stack switch: UNCONDITIONAL.  An extracode only ever comes from user, so this
        //     door never nests and needs no `СПСВ & 014' discriminator. ---
     15 vtm     [ustkbase]      // M15 := kernel stack base = frame base F

        // --- FULSAV: fill the reg.h frame at [M15 ..]; xts/its walk M15 F -> F+21 ---
        xta     save_a
        xts     save_r          // frame[0]  := A     ; A := R
        xts     save_y          // frame[1]  := R     ; A := Y
        its     ERET            // frame[2]  := Y     ; A := ERET (M032, live) <-- THE ONE CHANGE
        its     SPSW            // frame[3]  := RET   ; A := СПСВ (M027, live)
        xts     save_c          // frame[4]  := СПСВ  ; A := M16
        xts     save_sp         // frame[5]  := M16   ; A := interrupted M15
        its     14              // frame[6]  := M15   ; A := M14 (live -- the `$77 N' operand)
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

        // --- open the interrupt level for the C call, as kernel/besm6.S does: usys checks from
        //     inside a sysent stub that БлПр really is clear here (F_IPL) ---
        vtm     3               // ПСВ := БлП|БлЗ, БлПр clear: interrupts delivered

     13 vtm     intret          // tail call, as kernel/besm6.S does: syscall()'s own `13 uj'
        uj      syscall         //   returns straight to `intret'.  kernel/syscall.c -- THE REAL ONE

// badext -- extracodes э50-э76.  sysgate verbatim, calling badextr() instead.
        .globl  badext
badext:
        atx     save_a
        rte     07777
        atx     save_r
        yta
        atx     save_y
        ita     MOD
        atx     save_c
        ita     15
        atx     save_sp

     15 vtm     [ustkbase]

        xta     save_a
        xts     save_r
        xts     save_y
        its     ERET
        its     SPSW
        xts     save_c
        xts     save_sp
        its     14
        its     13
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
        its     1
        xts     save_a

        // --- open the interrupt level for the C call, as in sysgate ---
        vtm     3               // ПСВ := БлП|БлЗ, БлПр clear: interrupts delivered

     13 vjm     badextr         // kernel/syscall.c -- THE REAL ONE

        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret).  stx/sti pop
        //     frame[20..0], M15 walking F+21 -> F; old M15, Y, R and A are re-stashed to the
        //     cells and applied last, in the forced order Y -> A -> R (§7).
        //
        //     It shuts the door first, for the reason the original does: the gates above opened
        //     the level for their C call, and everything below reloads single registers (СПСВ,
        //     IRET) and the five shared cells, none of which survives a nested gate. ---
intret:
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр: external interrupts blocked

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
// ustkbase holds the base of the stack the gate switches to.  Unlike crt0t.S, which points it at
// its own `kstack', this one is filled at RUN TIME by usys.c with &u.u_stack[0] -- because the
// real syscall() finds its frame at u.u_stack, and the two must be the same place.
        .globl  ustkbase
ustkbase: .word 0

// uprog's link-time WORD address, as a plain integer -- the linker relocates the symbol into
// this word.  main() reads it to build the map; `(unsigned)&uprog' in C would hand back a fat
// pointer whose word address is not simply its low bits.
        .globl  uprogadr
uprogadr: .word uprog

        .bss
// The gates' temp cells.  In .bss, but it links low so a gate reaches them with a bare
// `atx save_a' -- an escape would clobber M[16] mid-save.  KEEP THESE FIRST.
save_a:  . = . + 1
save_r:  . = . + 1
save_y:  . = . + 1
save_c:  . = . + 1
save_sp: . = . + 1

// The u-area.  In the kernel this is the absolute page at 076000 (besm6.S: `u = 076000'); here it
// is ordinary storage, because a standalone test has no fixed page to give it.  It must be big
// enough for `struct user' AND the kernel stack that grows up from u.u_stack -- the frame plus
// syscall()'s C frames -- so two pages, twice what the kernel allots.
        .globl  u
u:      . = . + 4096

// main()'s own supervisor stack, separate from the gate's.
kstack: . = . + 512
