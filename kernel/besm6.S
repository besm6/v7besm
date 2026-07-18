// Machine-language assist for the BESM-6.
//
// This is the BESM-6 counterpart of the x86 machine assist kept in kernel/x86.s
// (the former mch.s).  It provides the small set of routines and global
// variables that the machine-independent C kernel cannot express in portable C:
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
#include "sys/besm6dev.h"
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

// Temp cells for the two asynchronous gates below -- `intrgate' (0501) and `trapgate' (0500).
// ONE copy serves both, and that is not an oversight:
//   * A cell is live only across a gate's prologue and its epilogue; in between (while the C
//     handler runs) the values already sit in the frame and the cells are dead.
//   * Both windows run with БлПр set -- the hardware forces it on when it takes the interrupt or
//     the fault, and only the closing `выпр' clears it -- so no external interrupt can land in
//     them, and the kernel runs unmapped (БлП = БлЗ = 1), so no fault can either.
//   * The gates therefore cannot overlap: neither can be re-entered, and neither can preempt
//     the other's live window.  (A second internal fault taken inside a gate is fatal anyway --
//     СПСВ and IRET are single registers, not a stack.)
// The epilogue is shared as well: `trapgate' jumps into `intret' below, which pops the frame and
// re-applies these cells, so the two gates MUST fill the same five.
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
// the kernel stack, filled by the Dubna FULSAV pipeline of `xts'/`its' (doc/Context_Switch.md
// §6).  That pipeline reaches M1-M14 and IRET/SPSW *live* -- it runs before `13 vjm extintr'
// clobbers anything -- so those need no cell.  The five below are the ones the pipeline cannot
// reach: A (it is the pipeline register), R and Y (only `rte'/`yta' read them, destroying A), the
// C register M16 (must be spilled before the stack-switch `u1a'/`vtm', whose utc for a long
// address would clobber it), and the interrupted M15 (the pipeline's own stack pointer, repointed
// by the switch).  See the gate and doc/Context_Switch.md §4/§14.
        .text
save_a:  .word 0                // A
save_r:  .word 0                // R   -- ALU mode (ω + the NTR suppress bits)
save_y:  .word 0                // Y (РМР) -- younger-bits register
save_c:  .word 0                // M16 -- the C register / address modifier
save_sp: .word 0                // interrupted M15 (the stack pointer)

// ============================================================================
// Kernel entry point
// ============================================================================

// _start -- first instruction executed by the kernel.
//
// On x86 this brings the CPU from real mode into paged 32-bit protected mode,
// clears BSS, sizes physical memory (into `phymem`), builds the initial page
// tables / GDT / IDT, programs the interrupt controller and timer, sets up the
// per-process u-area and kernel stack, then `call main` and finally drops into
// user mode running the `icode` bootstrap.
//
// TODO: implement BESM-6 bring-up.  The BESM-6 has no x86 real mode, no 8259/8253,
// and a different MMU, so this is written from scratch.  The required *outputs* of
// this phase are:
//   * BSS zeroed, from `edata` up to `end`;
//   * `phymem` set to the amount of physical memory found;
//   * the u-area and kernel stack mapped, interrupt vector installed;
//   * call main();  then enter user mode on the first process (icode).

// _start is deliberately tiny.  Everything the boot phase must do that can be said
// in C -- zeroing bss and publishing the memory size -- is done at the top of main()
// (see kernel/main.c).  Only the two things that cannot are here: seeding the stack
// pointer (r15) before any C runs, and calling main().  The machine resets into the
// kernel's own mode already (supervisor, unmapped, interrupts off, faults halt --
// doc/Memory_Mapping.md, "Reset state"), so there is nothing to set up.
//
// (bss is zeroed from C rather than here because the size, `end - edata', is a
// difference of two linker externals that b6as will not assemble -- see main.c.)

        .text
        .globl  _start
_start:
        // Seed the kernel stack pointer: the top of struct user, growing up to
        // 0100000.  ustkbase (machdep.c) holds &u.u_stack, a link-time constant.
     15 vtm [ustkbase]              // r15 := kernel stack base (~076214)
     13 vjm     main                // into the C kernel; main() zeroes bss, sizes memory
bhalt:  stop                        // main returned / faulted upstream: resumable halt
        uj      bhalt

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
//     IRET (doc/Context_Switch.md sec 8).  We reconcile them at ENTRY instead, for free: the fill
//     already reads a return register live, so it just reads ERET, and `intret' is reused
//     COMPLETELY UNMODIFIED.  Its `sti IRET' loads that address into М033 and its `3 ij' returns
//     through it -- which is correct, because the `ij' index field only selects WHICH REGISTER
//     holds the PC, while the mode (and so user-vs-supervisor) comes from СПСВ either way.
//
//   * NO PC FIXUP.  The extracode gate stores nextpc in ERET, so SPSW_NEXT_RK is never set by this
//     door and there is nothing to back up -- unlike the fault path, whose two lines of fixup sit
//     at the top of trap().
//
//   * THE STACK SWITCH IS UNCONDITIONAL, as in `trapgate': an extracode only ever comes from user
//     (the kernel never issues one), so this door never nests, the frame is always at [ustkbase],
//     and the C side takes void and finds it at u.u_stack.
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

     13 vjm     syscall             // syscall.c: marshal the arguments, dispatch, return

        uj      intret              // the shared epilogue in `intrgate' below

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

     13 vjm     badextr             // syscall.c: SIGINS

        uj      intret

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
// must be saved by hand.  Only two things differ:
//
//   * THE STACK SWITCH IS UNCONDITIONAL.  `intrgate' switches M15 only when the interrupted context
//     was user (СПСВ & 014 == 0), because an external interrupt legitimately nests inside the
//     kernel and must not have the stack pulled out from under the interrupted C frame.  A FAULT
//     from supervisor is a different animal: the user-access family validates its range up front
//     with useracc() and has no `nofault' path (see usermem.s), so a kernel-mode fault is a kernel
//     BUG, and trap() takes it to the `default' arm -- register dump, then panic.  Resetting M15
//     to the base of the kernel stack under a panic is harmless (it overwrites the interrupted
//     frames, which nothing is going to return to), so the discriminator buys nothing here and the
//     gate drops it.  This gate therefore never nests: the frame is always at [ustkbase].
//
//     That last sentence is why THE FRAME IS NOT PASSED TO C AT ALL.  Because the switch is
//     unconditional, the frame base is not a value the gate discovers at run time -- it is the
//     link-time constant `&u.u_stack', which trap() reads for itself.  So the call is the bare
//     `13 vjm trap', with no argument to build and no `14 vtm': a callee with NO PARAMETERS returns
//     M15 unchanged (doc/Besm6_Calling_Conventions.md, "On Return"), and the fill left M15 at
//     F+21, which is exactly `intret's precondition -- the same way `13 vjm extintr' works below.
//     The frame is still aliased IN PLACE, which is what reg.h promises its C readers: what
//     psig()/sendsig() write through u.u_ar0 is what the epilogue reloads into the registers.
//
//   * THE PC MUST BE BACKED UP.  Before vectoring, the machine advances past the faulting
//     instruction and records that it did so in СПСВ (SPSW_NEXT_RK, with SPSW_RIGHT_INSTR saying
//     which half of the word faulted) -- so the saved IRET points at the instruction AFTER the one
//     that faulted, and a plain `выпр' would skip it.  Undoing that is the kernel's job
//     (doc/Memory_Mapping.md, "The restart protocol").  It is done in C, at the top of trap(),
//     because the frame is aliased in place and two lines of C beat eight of branchy assembly.
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

     13 vjm     trap                // trap.c: back up the PC, decode ГРП, signal or grow

        uj      intret              // the shared epilogue in `intrgate' below

// ============================================================================
// External interrupt entry
// ============================================================================
//
// The hardware vector for an external (ГРП) interrupt is the fixed address 0501, laid down in
// the .const block above.  It jumps here; this gate saves the interrupted context into the
// canonical reg.h frame on the kernel stack, calls extintr() in intr.c, restores, and returns
// with `3 ij` (выпр).  doc/Context_Switch.md §4/§6/§7/§12/§14 is the authority: §4 the short
// save, §6 the FULSAV fill pipeline, §7 the forced restore order, §14 the four gaps below.
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
//            (doc/Context_Switch.md §13).  `ita`/`ati 16` move it as a plain register and do
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
// The gate does NOT point u.u_ar0 at the frame: the kernel sets u_ar0 in C (trap.c: u.u_ar0 =
// (int *)u.u_stack), and extintr() is frame-agnostic (takes void).  When clock() joins this path
// (task 15e) it should take void as well and find the frame the way trap() does -- but only from
// user, since THIS gate nests and the frame is then not at the stack base.  So the frame is built
// and reg.h-shaped, but u_ar0 is left to the C handler that first needs it.
//
// Interrupts stay blocked throughout (the hardware sets БлПр on entry, and `выпр` restores the
// mode word from SPSW on the way out), so one set of temp cells is safe -- intrgate cannot re-enter.
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
        //     `trapgate' (0500) jumps to `intret' rather than carry its own copy: the frame is the
        //     same reg.h frame, the five cells are the same five, and both doors leave through
        //     `3 ij' (М033 = IRET).  Its precondition is M15 = F+21 and A dead, which is where the
        //     C call leaves both gates.  What differs between the doors is entirely upstream --
        //     the stack-switch rule and the PC fixup -- so there is nothing to parameterise here. ---
intret:
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
// Interrupt priority level
// ============================================================================
//
// spl0()..spl7() and splx() now live in intr.c: on this machine raising the
// priority is one write of the МГРП mask (002 036), which is a table lookup and an
// intrinsic, so there is nothing left for assembly to do.

// ============================================================================
// Context switch
// ============================================================================
//
// int  save(label_t);            -- save current context; setjmp-like
// void resume(short paddr, label_t);  -- switch to another process; longjmp-like
//
// save() stores the callee-saved registers, stack pointer and return address
// into the label (a label_t = int[6]) and returns 0 on the direct call.  Later,
// when some other thread of control calls resume(paddr, label), execution
// reappears inside that save() returning NONZERO (1).  Callers branch on the
// result (see swtch()/newproc()/expand() in kernel/slp.c).
//
// resume() switches to the address space of the process whose swappable image is
// at physical click `paddr`, reloads registers/stack from `label`, and jumps --
// it does NOT return to its caller; control resurfaces in the matching save().
// resume must run with interrupts masked while it changes the address space.

// int save(label_t lbl)  -- one parameter (the label pointer, in A).
        .globl  save
save:
        // TODO: store r-callee-saved regs, r15 and the return address (r13) into
        // the label pointed at by A, then return 0 for the direct call.
        xta                      // return 0 (first-call semantics)
        13 uj

// void resume(short paddr, label_t lbl)  -- two parameters (paddr pushed, lbl in A).
        .globl  resume
resume:
        // TODO: mask interrupts; switch the address space to `paddr`; reload the
        // saved registers/stack from `lbl`; set A = 1 so the matching save()
        // "returns" nonzero; unmask; jump to the saved return address.  Does not
        // fall through to the caller.
        15 utm -1                // pop the one pushed argument (paddr)
        13 uj                    // placeholder: real code jumps into the resumed context

// ============================================================================
// Idle loop
// ============================================================================
//
// void idle(void) -- called from swtch() when no process is runnable.  Lower the
// priority to accept all interrupts, wait for an interrupt, then restore the
// priority and return; the scheduler re-scans the run queue.  The wait
// instruction's address is published in `waitloc` so clock() can charge the tick
// to idle time.
//
// TODO: drop to level 0, execute the BESM-6 "wait for interrupt", restore level.
        .globl  idle
idle:
        // TODO: enable all interrupts and halt until one arrives (see waitloc).
        13 uj

// ============================================================================
// Fault-safe access to user memory
// ============================================================================
//
// fubyte/fuword/subyte/suword/copyin/copyout -- fetch/store a byte or word, or copy
// a block, across the user/kernel boundary -- live in kernel/usermem.s, not here.
// On this machine the user's map is already in РП, so the copy needs no window: it
// just toggles БлП per word (read user mapped, store kernel unmapped, or the reverse)
// and validates the range up front with useracc() -- there is no `nofault' path.  They
// are in their own file for the same reason as uarea.s/seg.s: kernel/test/mmutest links
// the real routines, and besm6.o cannot enter a standalone test.  See usermem.s.

// ============================================================================
// Floating-point state
// ============================================================================
//
// x86's savfp/restfp/stst are gone with the u-area's FP block.  The BESM-6 has no
// separate floating-point register file -- the accumulator does float -- so there is no
// lazy FP context to save around a context switch, and nothing to restore.
//
// The arithmetic mode, however, does NOT ride in SPSW across a trap: SPSW carries only
// БлП/БлЗ/БлПр and the supervisor bits.  The ω-mode and the NTR suppress bits live in the
// mode register R, which the hardware neither saves nor restores; the trap gate saves R by
// hand (see intrgate above, and doc/Context_Switch.md §14).

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
// there is no window and no drainbrz: this is copyin's inner loop (usermem.s) minus
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
// TLB, interrupt flag
// ============================================================================
//
// void invd(void);   -- flush the TLB
// void cli(void);    -- disable interrupts (block external interrupts)
// void sti(void);    -- enable interrupts
//
// invd flushed the x86 address-translation cache after page-table edits (utab.c); on
// the BESM-6 it is gone, not stubbed -- writing a page register refills the corresponding
// TLB entries in the same instruction, so a stale translation cannot happen
// (doc/Memory_Mapping.md).  The x86 control-register reads ld_cr0/2/3 went with the x86
// panic dump in trap.c, and the x86 programmed-I/O primitives inb/outb/insw/outsw went
// with the x86 device drivers: the BESM-6 has no port space, so a peripheral is reached
// with 033 «увв» / 002 «рег» (doc/Besm6_Peripherals.md), not from here.
//
// cli/sti are the global interrupt disable/enable around short critical sequences.  On the
// BESM-6 that is the БлПр bit of PSW (M[021], bit 02000): set = external interrupts off.
// We read-modify-write PSW so the other mode bits (БлП/БлЗ/ПОП/ПОК) are preserved -- `vtm'
// would overwrite the whole word.  ita/ati take a 5-bit register number in supervisor mode,
// so M[021] = PSW is reachable (doc/Memory_Mapping.md).

        // invd is gone: on the BESM-6 `reg 020+i' refills the TLB as it writes RP,
        // so a stale translation is not a state the machine can be in and there is
        // nothing for utab.c to invalidate (doc/Memory_Mapping.md).

        .globl  cli
cli:                             // set БлПр -> external interrupts OFF
        ita     021              // A := PSW
        aox     #02000           //   set БлПр
        ati     021              // PSW := A
        13 uj

        .globl  sti
sti:                             // clear БлПр -> external interrupts ON
        ita     021              // A := PSW
        aax     #075777          //   clear БлПр (077777 & ~02000), keep the other bits
        ati     021              // PSW := A
        13 uj

// ============================================================================
// Global variables and data
// ============================================================================

        .data

// int phymem -- physical memory size in words (freed into coremap by startup()).
// main() sets it at boot (kernel/main.c); the kernel runs unmapped and cannot probe
// the store, so it is the fixed SIMH MEMSIZE rather than a scan.
        .globl  phymem
phymem: .word 0                  // set by main() during bring-up

// caddr_t waitloc -- address of the idle wait instruction; clock() compares the
// interrupted pc against it to charge idle time.
        .globl  waitloc
waitloc:.word 0                  // TODO: point at the idle-loop wait location

// int uhome -- the p_addr whose u-area is the one currently live at UBASE.  The
// live u-area is at 076000 and the copy in the process's image is stale between
// switches; resume() flushes and reloads when it finds uhome != the incoming
// p_addr.  See kernel/TODO.md, "The u-area invariant".
        .globl  uhome
uhome:  .word 0                  // set to proc[0].p_addr at boot by main() (kernel/main.c)

// struct user u -- the per-process user area: struct user at the bottom and the
// kernel stack growing up from it to 0100000.  It is not in the user's map and it
// is not in bss: it is the last page of the kernel's unmapped space, at a fixed
// PHYSICAL address, copied in and out of the process image on a context switch.
// So it is an absolute symbol, not storage -- b6as emits N_EXT|N_ABS and b6ld
// passes it through unrelocated.
        .globl  u
        u = 076000               // must agree with UBASE in include/sys/param.h
