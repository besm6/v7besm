// Machine-language assist for the BESM-6.
//
// This is the machine-dependent half of the kernel: the small set of routines and
// global variables that the machine-independent C kernel cannot express in portable C:
// boot-up, the trap/interrupt vector, context switching, fault-safe access to
// user memory, interrupt masking, and low-level device access.
//
// Every routine's *contract* (arguments, return value, side effects, role in the
// kernel) is specified in doc/Kernel_Assembly_Routines.md; the comment above each
// routine here restates the essentials.  This file is currently a SKELETON: the
// symbols exist so the kernel builds and links with the BESM-6 toolchain, but the
// bodies are mostly //TODO and will be implemented as the port progresses.
//
// ----------------------------------------------------------------------------
// Calling convention (see doc/Besm6_Calling_Conventions.md)
// ----------------------------------------------------------------------------
//   * Arguments are pushed on the stack in direct order; the LAST argument is
//     left in the accumulator (A), not pushed.
//   * r14 holds the argument count, negative.
//   * r13 holds the return address; return with `13 uj`.
//   * The result is returned in the accumulator.
//   * r1-r7 are callee-saved.
//   * r15 is the stack pointer (grows toward higher addresses).  On return a
//     callee with N>=1 parameters must pop the N-1 pushed arguments
//     (`15 utm -(N-1)`); the Nth was in A.  A callee with no parameters leaves
//     r15 unchanged.
//
// Because every C scalar is one 48-bit word (sizeof(int)==6 bytes==1 word) and
// the machine is word-addressed with no sub-word load/store, "byte" operations
// must be emulated by read-modify-write within a word.
//
// The skeleton bodies below follow the ABI so that, even unimplemented, they are
// valid no-op functions: value-returning stubs clear A with a bare `xta`, pop any
// pushed arguments, then `13 uj`.

// Number base: index-register operands (r1..r15) are written in DECIMAL; bit masks and the
// machine-register addresses (027 = SPSW, 020 = MOD = the C register M16) stay OCTAL, as they
// read in the machine documentation.

// Register indices used by the gates: SPSW the saved mode word, IRET the interrupt / fault return
// address (М033), ERET the extracode return address (М032), and MOD the C register M16 (the
// address modifier).  The bit masks (SPSW_EXTRACODE, SPSW_INTERRUPT, ...) come from the shared
// header; only these register indices are local here, since they are `ita'/`its'/`sti' operands.
//
// IRET and ERET are the ONE difference between the asynchronous gates and the synchronous ones:
// each door's frame fill reads the return register its own door uses (`its IRET' vs `its ERET')
// into the frame's single RET slot, and the shared epilogue reloads that slot into IRET and leaves
// through one hardcoded `3 ij' for all of them (see `intret' and include/sys/reg.h).
#include "sys/param.h"          // UBASE, NBUF, BSIZEW
#include "sys/besm6dev.h"
#include "sys/syscall.h"        // SYS_sigret, for the signal trampoline below
SPSW = 027                      // saved mode word (М027)
IRET = 033                      // interrupt / fault return address (М033)
ERET = 032                      // extracode return address (М032)
MOD  = 020                      // C register / address modifier (М020 = M16)

// The vectors below must land on their physical addresses, so this segment is laid
// out by absolute address with `.org'.  The origins hold only as long as the const
// segment really does begin at BADDR == 010 (cmd/ld/intern.h), which is what `.org'
// assumes: besm6.o must come first in the link (see OBJ in the Makefile) so nothing
// is merged in ahead of it, and `ld -T' must not be used.

        .const
        .org 0100               // Skip I/O region
#include "const.s"              // Put some #constants here to fill the space

        .org 0500               // Vectors for exceptions and interrupts are at 0500
      : uj trapgate             // 0500: internal interrupt (a fault) -- see below
      : uj intrgate             // 0501: external interrupt (ГРП) -- see below

        .org 0550               // Vectors for extracodes start at 0550
      : uj badext               // 0550: extracode *50
      : uj badext               // 0551: extracode *51
      : uj badext               // 0552: extracode *52
      : uj badext               // 0553: extracode *53
      : uj badext               // 0554: extracode *54
      : uj badext               // 0555: extracode *55
      : uj badext               // 0556: extracode *56
      : uj badext               // 0557: extracode *57

      : uj badext               // 0560: extracode *60, *20
      : uj badext               // 0561: extracode *61, *21
      : uj badext               // 0562: extracode *62
      : uj badext               // 0563: extracode *63
      : uj badext               // 0564: extracode *64
      : uj badext               // 0565: extracode *65
      : uj badext               // 0566: extracode *66
      : uj badext               // 0567: extracode *67

      : uj badext               // 0570: extracode *70
      : uj badext               // 0571: extracode *71
      : uj badext               // 0572: extracode *72
      : uj badext               // 0573: extracode *73
      : uj badext               // 0574: extracode *74
      : uj badext               // 0575: extracode *75
      : uj badext               // 0576: extracode *76
      : uj sysgate              // 0577: extracode *77 -- the syscall gate

// ============================================================================
// The user bootstrap
// ============================================================================
//
// int icode[] -- the program process 1 runs in USER mode to bring the system up.  It execs
// /etc/init with a one-entry argv; a successful exec never returns, so everything after the
// extracode is the failure path.
//
// IT IS COPIED TO ITS OWN LINK ADDRESS -- main() does `copyout(icode, icode, ...)' -- so the
// labels below are ordinary relocatable symbols.  That is the only way this block can name an
// address at all: b6as rejects `label - label', there is no PC-relative addressing, and the
// constant pool sits in the KERNEL's const segment at physical page 0, which is not what the
// user's virtual page 0 maps to.  Word 8 (BADDR) was not available: 010-077 is the device
// service-word area, overwritten by the root mount.  See doc/Unix_Context_Switch.md sec 10b.
        .globl  icode, eicode
icode:
        xta     iname           // A := (char *)"/etc/init"    -- argument 1
        xts     iargvp          // push it; A := (char **)argv -- argument 2, the last one
        $77     SYS_exec        // exec(name, argv): returns ONLY on failure
spin:   uj      spin            // ... and with no /etc/init there is nothing left to try

// `iname' and `iargv[0]' are FAT pointers -- marker in bit 48, byte offset 5 in bits 47-45,
// naming byte #0 -- because that is what namei() walks with fubyte().  The parentheses are
// load-bearing: `|' binds LOOSER than `+', so without them the relocatable term reaches `|'
// and b6as says "too complex expression".
iname:  .word   (.48 | .47 | .45) + istr        // char *  -- byte #0 of the string
iargvp: .word   iargv                           // char ** -- a plain word address
iargv:  .word   (.48 | .47 | .45) + istr        // argv[0]
        .word   0                               // argv[1] = NULL
istr:   .ascii  "/etc/init"                     // zero-padded to a word: NUL-terminated
eicode: .word   0                               // main() copies eicode - icode words.  The word
                                                //   itself is a placeholder, never copied: b6ld
                                                //   wants a const symbol to name a word its own
                                                //   file contributed (cmd/ld/symtab.c).

// Temp cells for the four gates below -- `sysgate'/`badext' (0577/0550), `trapgate' (0500) and
// `intrgate' (0501).  ONE copy serves all four, and that is not an oversight:
//   * A cell is live only across a gate's prologue and its epilogue; in between (while the C
//     handler runs) the values already sit in the frame and the cells are dead.
//   * BOTH LIVE WINDOWS RUN WITH БлПр SET, so no external interrupt can land in them, and the
//     kernel runs unmapped (БлП = БлЗ = 1), so no fault can either.  The two halves get that
//     guarantee from different places, and the difference matters:
//       - the PROLOGUE is covered by the hardware, which forces БлПр on when it takes the
//         interrupt, the fault or the extracode;
//       - the EPILOGUE covers itself.  The C handler does NOT run blocked -- the three
//         synchronous gates open the level for it, which is v7's spl0()-on-entry (see the call
//         sites below) -- and what it leaves behind is not the gate's to predict, so `intret'
//         re-blocks as its own first act rather than trusting the caller.
//   * The gates therefore cannot overlap: none can be re-entered, and none can preempt another's
//     live window.  (A second internal fault taken inside a gate is fatal anyway -- SPSW and IRET
//     are single registers, not a stack.)
// The epilogue is shared as well: every gate that RETURNS ends in `intret' below, which pops the
// frame and re-applies these cells, so they MUST fill the same five.  (The one path that does not
// return is `trapgate's supervisor arm, which branches to ktrap() and panics.)
//
// They live here in .const/.text (they link low -- under 0700, unix.nm) so every cell fits the
// 12-bit short-address field and the gate reaches them with a bare `atx save_a'.  This is not a
// nicety: a `< sym >' escape emits a `мода'/`utc' that loads M[16], so an escape ahead of the
// M[16] save would overwrite the very C register being saved.  The kernel's own data (bss) links
// far above 07777 and needs the escape; these cells do not.
//
// The ORDER of the five spills is a contract, not a style choice.  Two constraints fix it:
//
//   * `yta' copies Y into A ONLY in ω = logical mode.  In any other ω it merges Y into A's
//     mantissa and adjusts the exponent instead (doc/Besm6_Instruction_Set.md, 031 YTA), so a
//     gate that reads Y while the interrupted user happened to be in additive or multiplicative
//     mode saves garbage and restores garbage.  The interrupted ω is the USER's and can be
//     anything, so something must force logical before the `yta'.  The `rte' that saves R does
//     it: RTE reads R into A's exponent field and *then* sets ω = logical (030 RTE, "ω mode:
//     Logical").  That is why R must be saved before Y, and why nothing that changes ω may be
//     inserted between the two.  (The summary table in Besm6_Instruction_Set used to file RTE
//     under "reads R, then kept", which hides this; it now names the side effect explicitly.)
//   * The M16 spill must come BEFORE the stack switch: `[ustkbase]' assembles to a `wtc', which
//     loads M16 (see the escape note above), and a label's alignment filler in text is a `utc',
//     which loads it as well.  Anything that reaches M16 after the spill is harmless; anything
//     that reaches it before would have us frame the wrong value.
//
// Only FIVE cells, not the whole machine.  Each gate saves the interrupted context into the
// canonical reg.h frame (0 A, 1 R, 2 Y, 3 RET, 4 SPSW, 5 M16, 6 M15 ... 20 M1) built on
// the kernel stack, filled by the Dubna FULSAV pipeline of `xts'/`its' (doc/Dubna_Context_Switch.md
// §6).  That pipeline reaches M1-M14 and IRET/SPSW *live* -- it runs before `13 vjm extintr'
// clobbers anything -- so those need no cell.  The five below are the ones the pipeline cannot
// reach: A (it is the pipeline register), R and Y (only `rte'/`yta' read them, destroying A), the
// C register M16 (must be spilled before the stack-switch `u1a'/`vtm', whose utc for a long
// address would clobber it), and the interrupted M15 (the pipeline's own stack pointer, repointed
// by the switch).  See the gate and doc/Dubna_Context_Switch.md §4 and doc/Unix_Context_Switch.md §4.
        .text
save_a:  .word 0                // A
save_r:  .word 0                // R   -- ALU mode (ω + the NTR suppress bits)
save_y:  .word 0                // Y (РМР) -- younger-bits register
save_c:  .word 0                // M16 -- the C register / address modifier
save_sp: .word 0                // interrupted M15 (the stack pointer)

// The frame base `intrgate' publishes for clock() -- see that gate's header.  Here beside the
// temp cells for the same reason they are here: a bare 12-bit `atx intrframe' needs it below
// 07777, and an escape would emit a `мода' that clobbers M[16].
        .globl  intrframe
intrframe: .word 0              // struct trap * -- valid only inside `intrgate'

// ============================================================================
// Kernel entry point
// ============================================================================

// _start -- first instruction executed by the kernel, and the last one before user mode.
//
// It is two halves, as v7's is.  The FIRST is deliberately tiny: everything the boot phase
// must do that can be said in C -- zeroing bss and publishing the memory size -- is done at
// the top of main() (see kernel/main.c), so only the two things that cannot are here, seeding
// the stack pointer (r15) before any C runs and calling main().  The machine resets into the
// kernel's own mode already (supervisor, unmapped, interrupts off, faults halt --
// doc/Memory_Mapping.md, "Reset state"), so there is nothing to set up.
//
// The SECOND half is the first entry into user mode, and the one path into it that does not
// go through `intret': main() returns only in process 1, having copied the icode above into
// that process's image and loaded its map, and this forges the four registers `выпр' does not
// take from a frame and leaves.  See doc/Unix_Context_Switch.md.

        .text
        .globl  _start
_start:
        // Seed the kernel stack pointer: the top of struct user, growing up to
        // 0100000.  ustkbase (machdep.c) holds &u.u_stack, a link-time constant.
     15 vtm     [ustkbase]          // r15 := kernel stack base (~076214)
     13 vjm     main                // into the C kernel; main() zeroes bss, sizes memory

        // --- into user mode, at the icode.  main() returns HERE, and only in process 1;
        //     process 0 goes to sched() and never comes back. ---
        //
        // SHUT THE DOOR FIRST, as `intret' does: SPSW is a single register, and an interrupt
        // here would also see M15 pointing at the USER stack while SPSW still says supervisor.
        vtm     02003               // PSW := БлП|БлЗ|БлПр: interrupts blocked
     15 vtm     USTKPAGE*PGSZ       // r15 := 070000, the user stack base (sys/param.h)
      9 vtm     icode               // r9 := the icode's entry -- its own link address
      9 mtj     IRET                // М033 := entry: where `выпр' lands
        mtj     SPSW                // М027 := M[0] = 0: user mode, mapping and protection ON,
                                    //   БлПр clear.  `выпр' also drops ПоП/ПоК, so faults start
                                    //   vectoring exactly here (doc/Memory_Mapping.md, "выпр").
        ntr     7                   // R := RREG_C (reg.h): the mode word compiled code runs in
      3 ij                          // выпр: into the icode, in user mode

// ============================================================================
// Extracode entry
// ============================================================================
//
// The hardware does not funnel extracodes through 0500: it vectors э50-э77
// straight to 0550-0577, one word each, and hands the effective address to the
// handler in r14.  So the extracode gate is here, not in trap().
//
// э77 is the Unix system call (`$77 N', cf. cmd/sim and doc/Aout_Simulator.md); every other
// extracode is one we do not implement.  Each gets its own gate below -- `sysgate' calling
// syscall() and `badext' calling badextr(), both in kernel/syscall.c.
//
// BOTH ARE `trapgate' WITH EXACTLY ONE INSTRUCTION CHANGED: the FULSAV fill reads `its ERET'
// (М032) where the fault gate reads `its IRET' (М033).  The header over `trapgate' and the one
// over `intrgate' are the authority for every other step -- the five spills, why R, Y, M[16] and
// M15 must be saved by hand, and the shared `intret' epilogue.  Three things are worth stating
// here, because they are what makes the extracode door different:
//
//   * THE DOOR IS NORMALISED BY THE FILL, not by a copy.  The hardware puts an extracode's return
//     address in ERET and an interrupt's in IRET, and `выпр' is a single hardcoded `3 ij' (М033).
//     Dubna reconciles the two at EXIT, with the two-instruction OUTMACRO that copies ERET into
//     IRET (doc/Dubna_Context_Switch.md sec 8).  We reconcile them at ENTRY instead, for free: the fill
//     already reads a return register live, so it just reads ERET, and `intret' is reused
//     COMPLETELY UNMODIFIED.  Its `sti IRET' loads that address into М033 and its `3 ij' returns
//     through it -- which is correct, because the `ij' index field only selects WHICH REGISTER
//     holds the PC, while the mode (and so user-vs-supervisor) comes from SPSW either way.
//
//   * NO PC FIXUP.  The extracode gate stores nextpc in ERET, so SPSW_NEXT_RK is never set by this
//     door and there is nothing to back up -- unlike the fault path, whose two lines of fixup sit
//     at the top of trap().
//
//   * THE STACK SWITCH IS UNCONDITIONAL, as in `trapgate': an extracode only ever comes from user
//     (the kernel never issues one), so the DOOR never nests, the frame is always at [ustkbase],
//     and the C side takes void and finds it at u.u_stack.  (What does nest above that frame is an
//     external interrupt, once the gate opens the level below -- `intrgate's conditional switch is
//     there precisely for it, and clock() reads its frame out of `intrframe' for the same reason.)
//
//   * THE GATE OPENS THE INTERRUPT LEVEL before it calls C.  The hardware forces БлПр on at the
//     vector, and БлПр is this kernel's interrupt priority (kernel/intr.c), so without the three
//     instructions below a system call would run to completion with the clock stopped and every
//     device completion held off -- which is not what v7 does: the PDP-11 trap path drops the
//     priority to 0 before dispatching.  The enable sits AFTER the fill and not one instruction
//     earlier, because until the frame is complete SPSW/ERET and the five cells are the only copy
//     of the interrupted context and an interrupt would overwrite them (doc/Memory_Mapping.md,
//     "There is exactly one saved-state slot").  There is nothing else for the gate to update:
//     the level has no software shadow, PSW being the only copy of it (kernel/intr.c).
//     `trapgate' and `badext' do the same, for the same reason; `intrgate' does not -- a handler
//     runs at raised level and the return drops it, as `rtt' does on the PDP-11.
//
// The two gates are duplicated rather than sharing a body with a discriminator.  Nothing before
// the frame is filled can tell the doors apart -- the hardware identifies an extracode purely by
// WHICH VECTOR WORD it lands on -- so sharing would cost a flag cell and a branch to save a block
// that is otherwise identical.  This is the same call the file already makes for `trapgate' vs.
// `intrgate'; KEEP THE COPIES IN STEP.
//
// Both are exercised standalone against SIMH by kernel/test/crt0s.S + usys.c, which forges user
// mode, issues `$77 N' with arguments staged the real way, and checks ACC / r14 / r12 / r13 and a
// balanced r15 on the way back.

        .globl  sysgate
sysgate:
        // --- short save: the five registers the FULSAV pipeline cannot reach ---
        atx     save_a              // A first, as the vector does (frees A as scratch)
        rte     07777               // R -> A
        atx     save_r
        yta                         // Y -> A -- a full copy only because `rte' left ω = logical
        atx     save_y
        ita     MOD                 // M16 (C register) -> A -- must precede the stack switch,
        atx     save_c              //   whose `[ustkbase]' escape is a `wtc' that loads M16
        ita     15                  // interrupted M15 -> A -- before the stack switch repoints it
        atx     save_sp

        // --- stack switch: unconditional (see the header) ---
     15 vtm     [ustkbase]          // M15 := kernel stack base (~076214) = frame base F

        // --- fill the reg.h frame at [M15 ..] -- `trapgate's pipeline, with `its ERET' ---
        xta     save_a
        xts     save_r              // frame[0]  := A     ; A := R
        xts     save_y              // frame[1]  := R     ; A := Y
        its     ERET                // frame[2]  := Y     ; A := ERET (M032, live) <-- THE ONE CHANGE
        its     SPSW                // frame[3]  := RET   ; A := SPSW (M027, live)
        xts     save_c              // frame[4]  := SPSW  ; A := M16
        xts     save_sp             // frame[5]  := M16   ; A := interrupted M15
        its     14                  // frame[6]  := M15   ; A := M14 (live -- the `$77 N' operand)
        its     13                  // frame[7]  := M14   ; A := M13 (live)
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
        its     1                   // frame[19] := M2    ; A := M1
        xts     save_a              // frame[20] := M1    ; M15 := F+21 (above the frame); A discarded

        // --- open the interrupt level for the C call: v7's spl0()-on-entry (see the header).
        //     Safe only here -- the frame is complete, so the five cells and SPSW/ERET are dead.
        //     `vtm' with register field 0 is the mode write: БлП|БлЗ|БлПр from the address
        //     field, all three at once, nothing else in PSW touched.  This is spl0()'s own
        //     spl0 instruction -- __besm6_maskpsw(PSW_KERNEL) -- inlined rather than called. ---
        vtm     3                   // PSW := БлП|БлЗ, БлПр clear: interrupts delivered

     13 vtm     intret              // TAIL CALL: syscall.c marshals the arguments, dispatches, and
        uj      syscall             //   its own `13 uj' returns straight to `intret'.  `vjm'+`uj'
                                    //   costs a word more (doc/Unix_Context_Switch.md sec 10).

// badext -- extracodes э50-э76, which this kernel does not implement.  `sysgate' verbatim, ending
// in a different call: badextr() signals SIGINS rather than halting the machine.  Note э20/э60 and
// э21/э61 alias to one vector word each, and a user `стоп' arrives here as э63.

        .globl  badext
badext:
        // --- short save: the five registers the FULSAV pipeline cannot reach ---
        atx     save_a
        rte     07777
        atx     save_r
        yta
        atx     save_y
        ita     MOD
        atx     save_c
        ita     15
        atx     save_sp

        // --- stack switch: unconditional (an extracode only ever comes from user) ---
     15 vtm     [ustkbase]

        // --- fill the reg.h frame at [M15 ..] ---
        xta     save_a
        xts     save_r
        xts     save_y
        its     ERET                // the extracode return address, as in `sysgate'
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

        // --- open the interrupt level for the C call, as in `sysgate' ---
        vtm     3                   // PSW := БлП|БлЗ, БлПр clear: interrupts delivered

     13 vtm     intret              // tail call, as in `sysgate'
        uj      badextr             // syscall.c: SIGINS

// ============================================================================
// Internal-interrupt (fault) entry
// ============================================================================
//
// The hardware vector for an internal interrupt -- a protection violation, an illegal
// instruction, an instruction check -- is the fixed address 0500, laid down in the .const block
// above.  It jumps here; this gate saves the interrupted context into the canonical reg.h frame
// on the kernel stack, calls trap() in trap.c, and leaves through the shared epilogue `intret'
// below, which pops the frame and `выпр's (`3 ij', through М033 = IRET).
//
// A fault is asynchronous with respect to the register set -- it can land on any instruction --
// so the prologue, the FULSAV fill and the call are `intrgate's, instruction for instruction; the
// header over `intrgate' is the authority for every step of them, and for why R, Y, M[16] and M15
// must be saved by hand.  Only three things differ:
//
//   * THE STACK SWITCH IS UNCONDITIONAL.  `intrgate' switches M15 only when the interrupted context
//     was user (SPSW & 014 == 0), because an external interrupt legitimately nests inside the
//     kernel and must not have the stack pulled out from under the interrupted C frame.  A FAULT
//     from supervisor is a different animal: the user-access family validates its range up front
//     with useracc() and has no `nofault' path (see usermem.S), so a kernel-mode fault is a kernel
//     BUG, and trap() takes it to the `default' arm -- register dump, then panic.  Resetting M15
//     to the base of the kernel stack under a panic is harmless (it overwrites the interrupted
//     frames, which nothing is going to return to), so the discriminator buys nothing here and the
//     gate drops it.  This gate therefore never nests: the frame is always at [ustkbase].
//
//     That last sentence is why THE FRAME IS NOT PASSED TO C AT ALL.  Because the switch is
//     unconditional, the frame base is not a value the gate discovers at run time -- it is the
//     link-time constant `&u.u_stack', which trap() reads for itself.  So the call is the bare
//     tail jump `13 vtm intret' + `uj trap', with no argument to build and no `14 vtm': a callee
//     with NO PARAMETERS returns M15 unchanged (doc/Besm6_Calling_Conventions.md, "On Return"), and
//     the fill left M15 at F+21, which is exactly `intret's precondition -- the same way
//     `13 vjm extintr' works below.  (See `intret' for why the tail jump replaces `vjm'+`uj'.)
//     The frame is still aliased IN PLACE, which is what reg.h promises its C readers: what
//     psig()/sendsig() write through u.u_ar0 is what the epilogue reloads into the registers.
//
//   * THE PC MUST BE BACKED UP.  Before vectoring, the machine advances past the faulting
//     instruction and records that it did so in SPSW (SPSW_NEXT_RK, with SPSW_RIGHT_INSTR saying
//     which half of the word faulted) -- so the saved IRET points at the instruction AFTER the one
//     that faulted, and a plain `выпр' would skip it.  Undoing that is the kernel's job
//     (doc/Memory_Mapping.md, "The restart protocol").  It is done in C, at the top of trap(),
//     because the frame is aliased in place and two lines of C beat eight of branchy assembly.
//
//   * THE INTERRUPT LEVEL IS OPENED before the call, exactly as in `sysgate' -- see that gate's
//     header for why, and why not one instruction earlier.  It matters here for the same reason it
//     matters there: the fault a user process takes most often is the stack-growth fault, and
//     grow() sleeps.
//
//     BUT ONLY FOR A FAULT FROM USER, which is the one thing this gate does that `sysgate' does
//     not.  It reuses `intrgate's discriminator (SPSW & 014, zero iff the interrupted context was
//     user) -- and the supervisor arm does not merely skip the enable, it takes A DIFFERENT DOOR:
//     the `u1a' branches to ktrap(), the C routine for a kernel BUG, which dumps and panics and
//     never returns.  That path wants interrupts to stay exactly as it found them, for three
//     reasons that only became clear once the console driver was read:
//
//       - IT DOES NOT NEED THEM.  The register dump trap() prints is polled output: scputc()
//         (dev/sc.c) spins on the Consul's ready bit, and putchar() deliberately wraps it in
//         spl7() -- "printf() and panic() must work with the scheduler and the tty layer dead".
//       - WHAT COMES AFTER OPENS THEM ITSELF.  panic() calls update(), which sleeps on I/O, and
//         sleep() does its own spl0().  The dump is already out by then: trap() prints before it
//         calls panic().
//       - AND OPENING THEM RISKS THE DUMP, which is the only thing this path exists for.  Between
//         the gate and the printf, clock() and any armed driver handler would run on top of the
//         very structures whose corruption caused the fault; a hang or a second internal fault
//         there (STOP_DOUBLE_INTR) loses the diagnostic entirely.
//
//     It was unconditional at first, on the argument that a context about to panic cannot be
//     harmed.  What that missed is that the enable was only ever a few instructions wide anyway --
//     a fault inside an spl6 bracket carries that level in its cookie, so the first putchar's
//     `splx(s)' re-blocks after one character.  The discriminator
//     costs three instructions and makes the rule sayable: A FAULT FROM SUPERVISOR CHANGES
//     NOTHING ABOUT THE MACHINE'S INTERRUPT STATE.  (If a kernel-mode fault ever becomes
//     recoverable -- a `nofault' path, kernel copy-on-write -- revisit this with it.)
//
//   * SO THERE ARE TWO DOORS BEHIND THIS ONE VECTOR, and the branch is the whole of the split.
//     It costs nothing over a branch that merely skipped an instruction -- ktrap() returns to
//     nobody, so there is no return address to plant and no tail to jump back to -- and it buys
//     two routines that each say only what is true of them.  trap() sheds v7's USER bit and the
//     `+ USER' on every case label, because everything reaching it is from user; ktrap() sheds
//     the signal machinery, the grow() retry, the issig/psig tail, `intret', and the u.u_ar0
//     assignment (that name means the USER's registers -- publishing a kernel frame under it
//     would destroy the frame an interrupted syscall still writes through, and panic() does not
//     stop the machine before that matters: update() sleeps and other processes run on).
//     ktrap() carries one thing trap() must not: it dismisses EVERY fault bit in ГРП, not just
//     the one that fired, for that same reason -- a bit left standing would be read live by the
//     next process's trap() and shadow its real cause.
//
// The gate does NOT point u.u_ar0 at the frame; trap() does that itself, in C (`u.u_ar0 =
// (int *)u.u_stack'), for the same reason `intrgate' leaves it alone: doing it here would need a
// hardcoded u_ar0 offset for no runtime benefit.
//
// The same gate, duplicated for the same reason as `intrgate's copy, is exercised standalone against
// SIMH by kernel/test/crt0t.S + utrap.c, which forges user-mode entry, faults on a closed page and
// checks that the faulting instruction re-executes after the map is opened.

        .globl  trapgate
trapgate:
        // --- short save: the five registers the FULSAV pipeline cannot reach ---
        atx     save_a              // A first, as the vector does (frees A as scratch)
        rte     07777               // R -> A
        atx     save_r
        yta                         // Y -> A -- a full copy only because `rte' left ω = logical
        atx     save_y
        ita     MOD                 // M16 (C register) -> A -- must precede the stack switch,
        atx     save_c              //   whose `[ustkbase]' escape is a `wtc' that loads M16
        ita     15                  // interrupted M15 -> A -- before the stack switch repoints it
        atx     save_sp

        // --- stack switch: unconditional (see the header) ---
     15 vtm     [ustkbase]          // M15 := kernel stack base (~076214) = frame base F

        // --- fill the reg.h frame at [M15 ..] -- `intrgate's pipeline verbatim ---
        xta     save_a
        xts     save_r              // frame[0]  := A     ; A := R
        xts     save_y              // frame[1]  := R     ; A := Y
        its     IRET                // frame[2]  := Y     ; A := IRET (M033, live)
        its     SPSW                // frame[3]  := RET   ; A := SPSW (M027, live)
        xts     save_c              // frame[4]  := SPSW  ; A := M16
        xts     save_sp             // frame[5]  := M16   ; A := interrupted M15
        its     14                  // frame[6]  := M15   ; A := M14 (live)
        its     13                  // frame[7]  := M14   ; A := M13 (live)
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
        its     1                   // frame[19] := M2    ; A := M1
        xts     save_a              // frame[20] := M1    ; M15 := F+21 (above the frame); A discarded

        // --- pick the door.  A fault from SUPERVISOR is a kernel bug: it goes to ktrap(), which
        //     dumps and panics and never comes back, with the interrupt level left exactly as the
        //     hardware set it.  A fault from USER opens the level (grow() sleeps) and goes to
        //     trap().  See the header for why the level is not opened on both. ---
        ita     SPSW                // A := SPSW (still live -- nothing has issued an extracode)
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     ktrap               // РежЭ|РежПр set -> the kernel faulted.  A BRANCH, NOT A CALL:
                                    //   ktrap() never returns, so it needs no return address and
                                    //   no tail behind it.
        vtm     3                   // zero -> from user: PSW := БлП|БлЗ, interrupts delivered

     13 vtm     intret              // tail call, as in `sysgate'
        uj      trap                // trap.c: back up the PC, decode ГРП, signal or grow

// ============================================================================
// External interrupt entry
// ============================================================================
//
// The hardware vector for an external (ГРП) interrupt is the fixed address 0501, laid down in
// the .const block above.  It jumps here; this gate saves the interrupted context into the
// canonical reg.h frame on the kernel stack, calls extintr() in intr.c, restores, and returns
// with `3 ij` (выпр).  doc/Dubna_Context_Switch.md §4/§6/§7/§12 is the authority for the idioms:
// §4 the short save, §6 the FULSAV fill pipeline, §7 the forced restore order.  The four gaps
// below -- the registers the hardware does not save -- are doc/Unix_Context_Switch.md §1.
//
// An interrupt is asynchronous, so the calling convention's callee-saved set is not enough --
// the interrupted code owns EVERY register.  The hardware saves the interrupted PC in IRET
// (M[033]) and `выпр` restores the mode word from SPSW (M[027]); everything else that
// extintr() can touch is on us.  So the gate builds the full 21-word reg.h frame
//   0 A | 1 R | 2 Y | 3 RET | 4 SPSW | 5 M16 | 6 M15 | 7 M14 ... 20 M1
// with the Dubna FULSAV `xts'/`its' pipeline (§6): one store-and-load per word, the accumulator
// the pipeline register, M15 the frame pointer.  Because the fill runs BEFORE `13 vjm extintr',
// M1-M14 and IRET/SPSW are still live and go straight into the frame; only A, R, Y, M16 and
// the interrupted M15 are spilled to the five temp cells first (see the cells above for why each).
//
// Four of those saves are the §14 gaps the old subset gate got wrong, and all four still matter:
//
//   * R   -- the C ABI exits `NTR 3` / ω = logical (doc/Besm6_Runtime_Library.md), so extintr
//            returns with R changed; resume the user with it and its arithmetic mode is wrong.
//            The hardware never touches R (`выпр` does not restore it); software must.
//   * Y   -- any logical op (и/слц/сл) overwrites it, and the interrupted user may hold it live.
//   * M[16]  (the C register / address modifier) -- extintr() reaches globals with the
//            compiler's `utc name'+load idiom, clobbering it.  When a device interrupt lands in
//            the one-instruction window after a user `utc', SPSW carries SPSW_MOD_RK and the
//            closing `3 ij` re-arms the modifier -- from the clobbered value unless we save it
//            (doc/Dubna_Context_Switch.md §13).  `ita`/`ati 16` move it as a plain register and do
//            NOT arm the modifier, so saving/restoring the value is all that is needed.  It is
//            spilled up front, before the stack-switch `u1a'/`vtm' below whose utc would clobber it.
//   * M15 -- NOT banked on this machine (one stack register across modes).  When the interrupt
//            came from USER mode, M15 is the user stack pointer, and the trap has forced БлП on,
//            so that value is now an unmapped PHYSICAL word index pointing into the kernel image
//            below the u-area; run extintr()'s C frame on it and it silently corrupts the
//            kernel.  So repoint M15 at the kernel stack -- but ONLY from user, or a fault taken
//            mid-copyin (supervisor, БлП clear) would reset M15 out from under the syscall frame.
//            The signal is `SPSW & 014' (РежЭ|РежПр = RUU_EXTRACODE|RUU_INTERRUPT): zero IFF the
//            interrupted context was user.  Test that, not БлП (Memory_Mapping.md, "Entering and
//            leaving supervisor mode").  There is one kernel stack (the u-area is one fixed
//            physical page), so the switch is the constant load `_start` already uses -- and the
//            frame then builds at that base (from user) or on top of the interrupted kernel stack
//            (nested), which is exactly reg.h's "the frame lives on the kernel stack".
//
// THE GATE PUBLISHES THE FRAME BASE IN `intrframe', and this is the one door in the file that has
// to publish anything.  It is the direct consequence of the conditional switch above.  `trapgate'
// and `sysgate' switch unconditionally, so their frame base is not something they discover -- it is
// the link-time constant &u.u_stack, and trap()/syscall() just spell it (`(struct trap *)u.u_stack').
// Here the base is either [ustkbase] or the interrupted kernel r15, known only at run time, so
// clock() cannot spell it: on a tick nested inside a syscall, u.u_stack holds the SYSCALL's frame,
// whose SPSW says user -- clock() would charge u_utime and profile at a stale user PC.  Hence
// `ita 15' / `atx intrframe' at `extk' below.  It costs two instructions and no argument: the call
// stays the bare `13 vjm extintr', which returns M15 unchanged at F+21, `intret's precondition
// (doc/Besm6_Calling_Conventions.md, "On Return").
//
// It is a cell of its own and NOT u.u_ar0, which was the obvious candidate -- v7's own name for
// the current register frame, and what trap()/syscall() set.  Two reasons it is wrong here.  The
// store would have to be unconditional (from the kernel too, or clock() reads the wrong frame in
// exactly the nested case that motivates all this), so it would transiently overwrite the u_ar0 of
// an interrupted syscall -- and exec() (sys1.c) and sendsig() (machdep.c) write the resumed PC and
// the signal frame THROUGH u_ar0, from paths that sleep, so a tick landing between the assignment
// and the use sends those writes into a frame that is dead by the time anything resumes.  Banking
// the old value would work but cannot be done in C (the gate has already overwritten it by the
// time extintr() runs), so it would mean two more instructions here and two after the call.  And
// u_ar0 means the USER's saved registers; a tick nested in the kernel frames a KERNEL context, so
// pointing u_ar0 at it is a small lie even for the length of one call.  A private cell has neither
// problem.  One cell suffices for the same reason one `save_a' does: БлПр is held from the vector
// to the closing `выпр' -- THIS gate never opens it, unlike the three synchronous ones -- so this
// gate cannot re-enter itself.
//
// Interrupts stay blocked throughout THIS gate: the hardware sets БлПр on entry, nothing here
// clears it (extintr() and the handlers under it only ever raise the level, and extintr()'s closing
// repair moves the software shadow alone -- see intr.c), and `выпр' restores the mode word from
// SPSW on the way out.  That is what makes one set of temp cells safe here.  It is NOT what makes
// them safe in the three synchronous gates, which do open the level for their C call; there the
// epilogue re-blocks for itself, at the top of `intret' below.
//
// The same gate, and the reasoning behind it, is exercised standalone against SIMH by
// kernel/test/crt0u.s + uintr.c, which forges user-mode entry so the stack switch is tested.

        .globl  intrgate
intrgate:
        // --- short save: the five sensitive registers ---
        atx     save_a              // A first, as the vector does (frees A as scratch)
        rte     07777               // R -> A
        atx     save_r
        yta                         // Y -> A -- a full copy only because `rte' left ω = logical
        atx     save_y
        ita     MOD                 // M16 (C register) -> A -- must precede the stack switch,
        atx     save_c              //   whose `[ustkbase]' escape is a `wtc' that loads M16
        ita     15                  // interrupted M15 -> A -- before the stack switch repoints it
        atx     save_sp

        // --- stack switch: repoint M15 at the kernel stack ONLY when interrupted from user ---
        ita     SPSW                // SPSW -> A
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     extk                // РежЭ|РежПр set -> nested in the kernel: keep M15
     15 vtm     [ustkbase]          // zero -> from user: M15 := kernel stack base (~076214)
extk:                               // M15 = frame base F (ustkbase, or the interrupted kernel SP)

        // --- publish the frame base for clock() (see the header) ---
        ita     15                  // A := F -- the `aax' result it overwrites is dead here
        atx     intrframe           // a plain cell, bare-addressed: no escape, no M16 touched

        // --- fill the reg.h frame at [M15 ..] (§6).  xts/its always adjust M15, so it
        //     walks F -> F+21; M1-M14 and IRET/SPSW are read live, the five spills replayed. ---
        xta     save_a
        xts     save_r              // frame[0]  := A     ; A := R
        xts     save_y              // frame[1]  := R     ; A := Y
        its     IRET                // frame[2]  := Y     ; A := IRET (M033, live)
        its     SPSW                // frame[3]  := RET   ; A := SPSW (M027, live)
        xts     save_c              // frame[4]  := SPSW  ; A := M16
        xts     save_sp             // frame[5]  := M16   ; A := interrupted M15
        its     14                  // frame[6]  := M15   ; A := M14 (live)
        its     13                  // frame[7]  := M14   ; A := M13 (live)
        its     12                  // frame[8]  := M13   ; A := M12
        its     11                  // frame[9]  := M12   ; A := M11
        its     10
        its     9
        its     8
        its     7
        its     6
        its     5
        its     4
        its     3
        its     2
        its     1                   // frame[19] := M2    ; A := M1
        xts     save_a              // frame[20] := M1    ; M15 := F+21 (above the frame); A discarded

     13 vjm     extintr             // intr.c: read ГРП, dispatch, dismiss

        // --- restore: the exact inverse of the fill.  stx/sti pop frame[20..0], M15 walking
        //     F+21 -> F: M1-M14, M16 and RET/SPSW go straight back to their registers (the SPSW
        //     reload is a no-op -- extintr() left M027 untouched; RET reloads М033 for the `3 ij'),
        //     while old M15, Y, R and A are re-stashed to the cells and applied last, in the forced
        //     Y -> A -> R order (§7).  M1-M7 are ABI callee-saved, so their reload is redundant but
        //     keeps the pop symmetric.  A blank-address `stx' stores to word 0, which is discarded.
        //
        //     `trapgate' (0500), `sysgate' (0577) and `badext' (0550) jump to `intret' rather than
        //     carry their own copies: the frame is the same reg.h frame, the five cells are the same
        //     five, and every door leaves through `3 ij' (М033 = IRET).  Its precondition is
        //     M15 = F+21 and A dead, which is where the C call leaves all four gates.  What differs
        //     between the doors is entirely upstream -- the stack-switch rule, the PC fixup, which
        //     return register the fill reads -- so there is nothing to parameterise here.
        //
        //     THE FIRST THING IT DOES IS SHUT THE DOOR, and that is a precondition it enforces
        //     rather than inherits.  Everything below this point is unrepeatable: `sti SPSW' and
        //     `sti IRET' reload М027 and М033, single registers that the hardware overwrites the
        //     instant an interrupt is taken, and the tail re-stashes into the same five cells a
        //     nested gate would spill into.  `intrgate' arrives here already blocked, but the three
        //     synchronous gates opened the level for their C call and what the C side left behind is
        //     not knowable from here -- an spl bracket that ended in splx(0) is enough.  Three
        //     instructions here cover every door and every future tail; the alternative, a mode
        //     write in each C exit path, would have to be got right once per path forever.  A is dead on
        //     entry (the `stx' below overwrites it) and M16 is reloaded from frame[5] further
        //     down. ---
intret:
        vtm     02003               // PSW := БлП|БлЗ|БлПр: interrupts blocked, as spl1() does
                                    // it in C with __besm6_maskpsw(PSW_KERNEL|PSW_INTR_DISABLE)

        stx                         // A := M1 from frame[20]
        sti     1                   // restore M1; A := M2 from frame[19]
        sti     2
        sti     3
        sti     4
        sti     5
        sti     6
        sti     7
        sti     8
        sti     9
        sti     10
        sti     11                  // restore M11;  A := M12 from frame[9]
        sti     12                  // restore M12;  A := M13 from frame[8]
        sti     13                  // restore M13;  A := M14 from frame[7]
        sti     14                  // restore M14;  A := M15 from frame[6]
        stx     save_sp             // prepare M15;  A := M16 from frame[5]
        sti     MOD                 // restore C;    A := SPSW from frame[4]
        sti     SPSW                // restore SPSW; A := RET from frame[3]
        sti     IRET                // restore RET into М033 (IRET);  A := Y from frame[2]
        stx     save_y              // prepare Y;    A := R from frame[1]
        stx     save_r              // prepare R;    A := A from frame[0]
        atx     save_a              // prepare A
        xta     save_y              // Y back, via the `aex` side effect (§7)
        aex                         //   (A is garbage after this -- intentional)
        xta     save_sp             // interrupted M15 back (M15 no longer names the frame -- fine)
        ati     15
        xta     save_a              // A back (xta does not disturb Y)
        xtr     save_r              // R back -- must be last (a later xta would perturb it)
      3 ij                          // return from interrupt via M[033]=IRET

// ============================================================================
// The signal-return trampoline
// ============================================================================
//
// int sigcode[1] -- ONE INSTRUCTION WORD, executed in USER mode, and the only piece of
// this kernel that ever runs there.
//
// sendsig() (kernel/sendsig.c) builds a signal frame on the user stack and copies this
// word out just above it, then enters the handler with r13 -- the return-address
// register -- naming the copy.  A handler is an ordinary C function and returns with
// `13 uj', so what it returns TO has to be executable; this is that instruction, and
// it takes the process straight back into the kernel through the 0577 door, where
// sigret() reloads the frame.
//
// It is assembled here rather than spelled as a constant in C so that NOTHING writes
// down an opcode encoding: `$77 SYS_sigret' is the same source form libc's syscall
// leaves use, and the number comes from <sys/syscall.h> like every other.
//
// The bare `:' forces word alignment, as it does in the vector table above -- the word
// is copied out whole and must hold the extracode in its LEFT half.  b6as then TALIGNs
// after the extracode and fills the right half with an empty instruction, which is
// never reached: an extracode returns to the left half of the NEXT word, and sigret()
// does not return here at all.
      :
        .globl  sigcode
sigcode:
        $77     SYS_sigret

// ============================================================================
// Execution profiling
// ============================================================================
//
// void addupc(int pc, void *prof, int incr) -- profil(2) support.
//
// Map the interrupted user pc into a bucket of the profiling descriptor `prof`
// { short *pr_base; unsigned pr_size, pr_off, pr_scale; } and add `incr` to that
// bucket in user space (through the nofault guard).  Called from clock() and
// trap() when profiling is armed.
//
// TODO: implement the scaled bucket computation and the fault-safe user update.
        .globl  addupc
addupc:
        // TODO: bucket = ((pc - pr_off) * pr_scale) >> N; bump pr_base[bucket].
        xta                      // no meaningful return value
        15 utm -2                // pop the two pushed arguments (pc, prof)
        13 uj

// ============================================================================
// Kernel memory primitives
// ============================================================================
//
// void wcopy(const void *src, void *dst, unsigned nwords) -- kernel block copy
// void wzero(void *dst, unsigned nwords)                  -- kernel block zero
//
// Plain kernel-to-kernel operations (no fault protection).  On the word-addressed
// BESM-6 the count is a WORD count, not a byte count: callers convert with btow()
// (sys/param.h), so there is no six-chars-per-word tail to handle -- these are pure
// word loops.  The kernel runs unmapped (a kernel address IS a physical address), so
// there is no window and no drainbrz: this is copyin's inner loop (usermem.S) minus
// the per-word БлП toggle, the useracc validation and the PSW save.  Register-only:
// r10/r11/r12 (caller-saved) hold the count and the two pointers; the word rides in A;
// r1-r7, r13 and r15 are untouched.

// void wcopy(src, dst, nwords)  -- three parameters (src, dst pushed; nwords in A).
        .globl  wcopy
wcopy:
        ati     012              // r10 := nwords (arrived in A)
     15 xta     -2               // A := src   (1st pushed argument)
        aax     #077777          //   strip the fat-pointer marker/offset -> word address
        ati     013              // r11 := src word pointer
     15 xta     -1               // A := dst   (2nd pushed argument)
        aax     #077777
        ati     014              // r12 := dst word pointer
wc_lp:
     10 vzm     wc_dn            // count == 0 -> done
     11 xta                      // A := mem[r11]   (unmapped = physical)
     12 atx                      // mem[r12] := A
     11 utm     1                // ++src
     12 utm     1                // ++dst
     10 utm     -1               // --count
        uj      wc_lp
wc_dn:
     15 utm     -2               // pop the two pushed arguments (src, dst)
     13 uj

// void wzero(dst, nwords)  -- two parameters (dst pushed; nwords in A).
        .globl  wzero
wzero:
        ati     012              // r10 := nwords (arrived in A)
     15 xta     -1               // A := dst   (the one pushed argument)
        aax     #077777
        ati     014              // r12 := dst word pointer
        xta                      // A := 0 (physical word 0 reads 0 unmapped, cf. copyin)
wz_lp:
     10 vzm     wz_dn            // count == 0 -> done
     12 atx                      // mem[r12] := 0
     12 utm     1                // ++dst
     10 utm     -1               // --count
        uj      wz_lp
wz_dn:
     15 utm     -1               // pop the one pushed argument (dst)
     13 uj

// ============================================================================
// Global variables and data
// ============================================================================

// struct user u -- the per-process user area: struct user at the bottom and the
// kernel stack growing up from it to 0100000.  It is not in the user's map and it
// is not in bss: it is the last page of the kernel's unmapped space, at a fixed
// PHYSICAL address, copied in and out of the process image on a context switch.
// So it is an absolute symbol, not storage -- b6as emits N_EXT|N_ABS and b6ld
// passes it through unrelocated.
        .globl  u
        u = UBASE

// char buffers[NBUF][BSIZE] -- page aligned buffers for disk I/O.  Declared `extern' in
// main.c: like `u' this is a fixed physical area, not bss, so the storage is named here.
        .globl  buffers
        buffers = u - NBUF*BSIZEW
