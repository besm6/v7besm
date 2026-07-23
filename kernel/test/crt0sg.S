//
// crt0sg.S -- forge-entry startup for the from-USER-mode SIGNAL test (usig).
//
// The fifth of the forge tests, and the only one that goes to user mode, comes back, goes to a
// DIFFERENT place in user mode and comes back again.  It exercises the signal frame
// (kernel/sendsig.c) on the machine: sendsig() builds it, the gate's epilogue enters the handler
// through it, the handler returns into the `$77 SYS_sigret' word sendsig planted on the user
// stack, and sigret() reloads the frame.
//
// It links BOTH doors, because a signal is delivered at both:
//   * 0577 -> sysgate: the syscall return (kernel/syscall.c's sysret(), which is where psig()
//     runs) -- and, a moment later, the planted extracode the handler returns into.
//   * 0500 -> trapgate: the fault return (trap()), and this is the leg no user-mode return path
//     could ever have handled.  uprog faults from the RIGHT HALF of a word, so resuming it
//     correctly means restoring SPSW_RIGHT_INSTR out of the frame -- something only `выпр' can
//     do, and the whole reason sigret() is a syscall rather than a few instructions in libc.
//
// Three things are under test that no other test can see:
//   * THE PLANTED WORD REALLY EXECUTES.  It is a word the KERNEL stored into a data page and the
//     user then jumps to.  The machine tags every word it stores with a свертка (SIMH: the tag is
//     `RUU ^ PARITY_INSN', besm6_mmu.c), and instruction fetch checks it -- so if this kernel ever
//     acquired a reason to set ПКП/ПКЛ, a handler would return into an instruction-check fault
//     instead of into the kernel.  Nothing but running it on the machine proves this.
//   * THE HANDLER'S ENTRY ABI.  A handler is a one-argument C function: the number in the
//     accumulator, r14 = -1, r13 the return address (doc/Besm6_Calling_Conventions.md).
//   * THE FRAME RESTORES EVERYTHING.  uprog holds witnesses in r9 and r11 and reads the syscall's
//     own results (accumulator, r12, r14) AFTER the handler has run and deliberately clobbered
//     all five.
//
// The pieces:
//   * main() (usig.c) programs a user map with sureg(), points ustkbase at u.u_stack and calls
//     gouser(), exactly as usys does.
//   * uprog runs two legs; the handlers are user code here too, at virtual page 0 beside it.
//   * usig.c's psig() and trap() call the REAL sendsig(), from kernel/sendsig.o, and sysent[45]
//     holds the REAL sigret().  Only the two gates are duplicated, for the standing reason that
//     besm6.o cannot enter a standalone test.
//
// The forged values, the map and the slot addresses are shared with usig.c through these EQUs;
// keep the two in step.  See doc/Kernel_Assembly_Routines.md section 3 for the gates.

#include "sys/besm6dev.h"       // SPSW_* mode-word bits (shared with kernel/besm6.S)
#include "sys/syscall.h"        // SYS_sigret, for the trampoline word below

// Machine-register indices for the gates and gouser, matching kernel/besm6.S.
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
ERET = 032                      // extracode return address (М032)
MOD  = 020                      // C register / address modifier (М020 = M16)

PGSZ    = 02000                 // 1024 words per page (octal)

// -- forged register values (gouser plants them; usig.c checks what came back) --------------
RMODE   = 044                   // R: ω = logical (bits 5-3 = 001) + overflow-suppress (bit 6)
RMR     = 0123456               // Y: a distinctive younger-bits value
MODVAL  = 04010                 // M[16]: inert (the forged SPSW does not arm the modifier)
R13V    = 054321                // r13: the caller's return address, which nothing may touch --
                                //   not the syscall, and not the signal frame either
USPV    = 070010                // r15: the user stack, virtual page 28 (USTKPAGE).  MAPPED and
                                //   writable, because the signal frame is built on it
KSENT   = 0333333               // usig.c seeds physical USPV/USPV+1 with this

CLOSEDPG = 010000               // virtual page 4: never mapped -- uprog's fault leg

// -- witnesses uprog holds across a delivery ------------------------------------------------
R9V     = 01234                 // r9  before the syscall; the handler overwrites it
R11V    = 02345                 // r11 before the syscall; likewise
CLOBBER = 0777                  // what the handler leaves in r9, r11, r12, r14 and A

// -- report slots, in virtual page 2 (mapped from the start; usig.c reads them physically) --
VS_ACC   = 04000                // leg 1, after the round trip: the accumulator (r_val1 again)
VS_ERR   = 04001                //   r14 (0 again)
VS_VAL2  = 04002                //   r12 (r_val2 again)
VS_R13   = 04003                //   r13 (still R13V)
VS_R15   = 04004                //   r15 (still USPV: the frame was popped)
VS_R9    = 04005                //   r9  (still R9V)
VS_R11   = 04006                //   r11 (still R11V)
VS_SIG1  = 04010                // handler 1: the signal number it was given
VS_HR14  = 04011                //   r14 at entry -- the negative argument count, -1
VS_HR13  = 04012                //   r13 at entry -- the planted word, at USPV + 21
VS_HR15  = 04013                //   r15 at entry -- one above it
VS_SIG2  = 04014                // handler 2: the signal number, from the FAULT leg

// ----------------------------------------------------------------------------
// Interrupt and extracode vectors
// ----------------------------------------------------------------------------
// crt0sg.o is linked first, so its .const begins at BADDR (010) and `.org' lands the vectors on
// their hardware addresses (same rule as kernel/besm6.S).  The trailing `.org PGSZ' pads the
// const section to a full page so the .text below -- and with it uprog -- links into a NON-zero
// physical page: a zero РП descriptor is non-executable, so uprog cannot live in physical page 0.

        .const
        .org 0500
        uj      trapgate        // 0500: internal interrupt -- uprog's leg-2 fault, and the exit
      : uj      badintr         // 0501: external interrupt -- none is expected here
        .org 0577
        uj      sysgate         // 0577: extracode э77 -- the syscall gate AND the planted word
        .org PGSZ               // pad the const page so .text starts in physical page >= 1

// ----------------------------------------------------------------------------
        .text
        .globl  _start
_start:
     15 vtm     kstack          // main()'s own supervisor stack (the gates use u.u_stack)
        vtm     02003           // PSW := БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
     13 vjm     main            // usig.c: build the map, seed the sentinels, call gouser()
halt0:  stop                    // main() should never return (gouser does not)
        uj      halt0

// void halt(unsigned mask) -- the test's exit.  One parameter, so the mask arrives in the
// accumulator, which is exactly where usig.ini wants it.  Called from report(); never returns.
        .globl  halt
halt:   stop                    // halt with the fault mask in ACC
        uj      halt

// An external interrupt would mean МГРП was left unmasked; halt distinguishably.
badintr: stop   01
        uj      badintr

// int sigcode[1] -- kernel/besm6.S's word, duplicated here with the gates and for the same
// reason.  sendsig() copies it onto the user stack as the handler's return address; the bare `:'
// word-aligns so that the extracode is the word's LEFT half, and b6as TALIGNs after it.
      :
        .globl  sigcode
sigcode:
        $77     SYS_sigret

// ----------------------------------------------------------------------------
// void gouser(unsigned uentry) -- forge a user context and enter it.
// ----------------------------------------------------------------------------
// crt0s.S's, unchanged.  The forged SPSW is ZERO: user mode, mapping and protection on, the
// modifier not armed and SPSW_RIGHT_INSTR clear, so `выпр' lands on the LEFT half of uprog's
// first word -- which is where the half-word layout below is counted from.
        .globl  gouser
gouser:
        ati     IRET            // IRET (M[033]) := uentry -- where `выпр' lands
        xta   #(MODVAL)
        ati     MOD             // M[16] := MODVAL -- inert, but the gates must preserve it
        xta                     // forged SPSW := 0 (user mode, nothing armed)
        ati     SPSW            //   M[027] := it
     15 vtm     USPV            // r15 := the user stack: the signal frame is built here
     13 vtm     R13V            // r13 := the caller's return address (gouser does not return)
        ntr     RMODE           // R := RMODE (logical + overflow-suppress)
        xta   #(RMR)
        aex                     // Y := RMR, via the `aex'-with-blank side effect (keeps R)
      3 ij                      // выпр: into uprog, user mode

// ----------------------------------------------------------------------------
// uprog -- the user program.  Runs MAPPED at virtual page 0.
// ----------------------------------------------------------------------------
// As in crt0s.S: no `#(...)' pool constants (the literal pool is in .const at physical page 0,
// which is not what virtual page 0 maps to here), so constants arrive through `vtm' and `ita'.
// r10 is the store pointer and r9/r11 are the witnesses the handler clobbers.
        .globl  uprog
uprog:
        // --- leg 1: a signal delivered at a SYSCALL return -------------------------------
        // sysent[20] is a getpid-shaped stub: it sets both results and arms the delivery.  On
        // the way out syscall() writes them into the frame and then sysret() calls psig(),
        // which calls sendsig() -- so the frame that goes on the user stack carries this call's
        // OWN results, and getting them back below is the proof it was restored.
      9 vtm     R9V             // witnesses, live across the delivery
     11 vtm     R11V
        $77     20              // -> sysgate -> syscall() -> stub_two -> psig() -> sendsig()
                                //    -> handler1 -> the planted word -> sigret() -> here
     10 vtm     VS_ACC
     10 atx     0               // the accumulator: r_val1, restored
        ita     016
     10 vtm     VS_ERR
     10 atx     0               // r14: 0 on success, restored
        ita     014
     10 vtm     VS_VAL2
     10 atx     0               // r12: r_val2, restored
        ita     015
     10 vtm     VS_R13
     10 atx     0               // r13: the caller's return address, never touched
        ita     017
     10 vtm     VS_R15
     10 atx     0               // r15: the frame was popped, so it is USPV again
        ita     011
     10 vtm     VS_R9
     10 atx     0               // r9: the witness, restored
        ita     013
     10 vtm     VS_R11
     10 atx     0               // r11: likewise

        // --- leg 2: a signal delivered at a FAULT return, from a RIGHT-half instruction ---
        // The next word is the whole leg.  Its LEFT half bumps a counter and its RIGHT half
        // faults on a closed page; trap() (usig.c) delivers a signal from the first fault and
        // takes the second as "done".
        //
        // If the frame round trip lost SPSW_RIGHT_INSTR, sigret() would resume at the LEFT half
        // instead and the counter would be bumped twice.  usig.c reads it out of the SECOND
        // fault's frame -- r11 is in there -- and insists on exactly one.
     11 vtm     0               // r11 := 0: the left-half execution counter
     14 vtm     CLOSEDPG
      :                         // word-align, so the pair below is one word
     11 utm     1               // LEFT:  bump the counter
     14 xta     0               // RIGHT: fault -> trapgate -> trap() -> sendsig() -> handler2
                                //        -> the planted word -> sigret() -> back to THIS half
        uj      uprog           // (never reached: the second fault reports and halts)

// ----------------------------------------------------------------------------
// handler1, handler2 -- the signal handlers.  USER code, entered by the frame.
// ----------------------------------------------------------------------------
// The entry ABI is the calling convention's, for a function of one argument: the number in the
// accumulator, r14 = -(1), r13 the return address -- which here is the word sendsig() planted
// just above the frame, on the user stack.  Neither handler has a prologue: `b$save' would be a
// call, and everything they need is already in registers.
//
// handler1 records what it was given and then CLOBBERS every register the frame owes the
// interrupted code -- the accumulator, r9, r11, r12 and r14 -- so that uprog's checks above have
// something to fail on.  r13 is left alone: it is how it gets back.
        .globl  handler1
handler1:
     10 vtm     VS_SIG1
     10 atx     0               // the signal number, straight out of the accumulator
        ita     016
     10 vtm     VS_HR14
     10 atx     0               // r14: the negative argument count
        ita     015
     10 vtm     VS_HR13
     10 atx     0               // r13: the planted word
        ita     017
     10 vtm     VS_HR15
     10 atx     0               // r15: one above it

      9 vtm     CLOBBER         // now wreck everything the frame is responsible for
     11 vtm     CLOBBER
     12 vtm     CLOBBER
     14 vtm     CLOBBER
        ita     011             // ... including the accumulator
     13 uj                      // return: execute the planted `$77 SYS_sigret'

// handler2 -- the fault leg's.  It only has to run and return; what leg 2 is watching is where
// sigret() puts the program back.
        .globl  handler2
handler2:
     10 vtm     VS_SIG2
     10 atx     0               // the signal number
     12 vtm     CLOBBER
     14 vtm     CLOBBER
        ita     014
     13 uj                      // return through the planted word

// ----------------------------------------------------------------------------
// The gates -- copies of kernel/besm6.S:sysgate and :trapgate, with the shared epilogue
// (`intret', which lives in intrgate over there) spliced in behind them.
// ----------------------------------------------------------------------------
// besm6.o cannot enter a standalone test, so the gates are duplicated here, exactly as crt0s.S
// and crt0t.S duplicate them.  Keep them identical in LOGIC to kernel/besm6.S; that file's
// headers are the authority for every step, and doc/Unix_Context_Switch.md for the frame.
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
        ita     MOD             // M16 (C register) -> A -- must precede the stack switch
        atx     save_c
        ita     15              // interrupted M15 -> A -- before the switch repoints it
        atx     save_sp

        // --- stack switch: UNCONDITIONAL (an extracode only ever comes from user) ---
     15 vtm     [ustkbase]      // M15 := kernel stack base = frame base F

        // --- FULSAV: fill the reg.h frame at [M15 ..]; xts/its walk M15 F -> F+21 ---
        xta     save_a
        xts     save_r
        xts     save_y
        its     ERET            // the extracode return address -- THE ONE CHANGE from trapgate
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

        vtm     3               // PSW := БлП|БлЗ, БлПр clear: interrupts delivered

     13 vtm     intret          // tail call: syscall()'s own `13 uj' returns to `intret'
        uj      syscall         // kernel/syscall.c -- THE REAL ONE

// trapgate -- the fault door.  sysgate with `its IRET' for `its ERET', and with the supervisor
// arm kernel/besm6.S carries: a fault from the kernel is a bug and goes to ktrap(), which here
// is a stub that halts with a mask bit.
        .globl  trapgate
trapgate:
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
        its     IRET            // the fault return address (M033)
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

        ita     SPSW            // A := SPSW (still live)
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     ktrap           // РежЭ|РежПр set -> the kernel faulted.  A branch, not a call
        vtm     3               // zero -> from user: interrupts delivered

     13 vtm     intret          // tail call, as in sysgate
        uj      trap            // usig.c: back up the PC, deliver a signal or report

        // --- restore: the exact inverse of the fill (kernel/besm6.S:intret).  It shuts the
        //     door first: both gates above opened the level for their C call, and everything
        //     below reloads single registers (SPSW, IRET) and the five shared cells. ---
intret:
        vtm     02003           // PSW := БлП|БлЗ|БлПр: external interrupts blocked

        stx                     // A := M1 from frame[20]
        sti     1
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
// The base of the stack the gates switch to.  Filled at RUN TIME by usig.c with &u.u_stack[0],
// because the real syscall() and the real sendsig() both find the frame at u.u_stack.
        .globl  ustkbase
ustkbase: .word 0

// Link-time WORD addresses, as plain integers -- the linker relocates each symbol into its word.
// main() reads them to build the map and to compute the handlers' VIRTUAL addresses; `(unsigned)
// &uprog' in C would hand back a fat pointer whose word address is not simply its low bits.
        .globl  uprogadr, handler1adr, handler2adr
uprogadr:    .word uprog
handler1adr: .word handler1
handler2adr: .word handler2

        .bss
// The gates' temp cells.  In .bss, but it links low so a gate reaches them with a bare
// `atx save_a' -- an escape would clobber M[16] mid-save.  KEEP THESE FIRST.
save_a:  . = . + 1
save_r:  . = . + 1
save_y:  . = . + 1
save_c:  . = . + 1
save_sp: . = . + 1

// The u-area.  In the kernel this is the absolute page at 076000; here it is ordinary storage,
// big enough for `struct user' AND the kernel stack that grows up from u.u_stack.
        .globl  u
u:      . = . + 4096

// main()'s own supervisor stack, separate from the gates'.
kstack: . = . + 512
