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

        .const
        . = . - 010 + 0100      // Skip I/O region
#include "const.s"              // Put some #constants here to use this space
      : uj trap                 // 0500: internal interrupt (a fault)
      : uj extint               // 0501: external interrupt (ГРП) -- see below
        . = . - 0502 + 0550
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
      : uj syscall              // 0577: extracode *77 -- the syscall gate

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

// int edata -- first word past the data segment.
// int end   -- first word past everything (const + text + data + bss).
//
// Both are boundary symbols defined by b6ld, which creates them as soon as they
// are referenced -- hence the .globl here, and hence no `kend' variable of our
// own to fill in at boot.  See doc/Linker_Manual.md, section 4.3.
        .globl  edata
        .globl  end

        .text
        .globl  _start
_start:
        // TODO: BESM-6 processor initialization.
        // TODO: zero bss (edata..end), set phymem, map u-area + kernel stack.
        // TODO: install interrupt vector; call main; enter user mode.
     13 vjm main
        stop                        // placeholder

// ============================================================================
// Extracode entry
// ============================================================================
//
// The hardware does not funnel extracodes through 0500: it vectors э50-э77
// straight to 0550-0577, one word each, and hands the effective address to the
// handler in r14.  So the extracode gate is here, not in trap().
//
// э77 is the Unix system call (`$77 N', cf. cmd/sim and doc/Aout_Simulator.md);
// every other extracode is one we do not implement.  Both are skeletons for now
// -- they halt with a recognizable code, the way test/crt0.s's fault stub does,
// rather than jump into C with no frame under them.
//
// TODO (task 15): build the trap frame, point u.u_ar0 at it, call the C syscall
// dispatcher, restore, and return with `2 ij' (выпр through ЭРЕТ, not ИРЕТ).

        .globl  syscall
syscall: stop   077              // TODO: the syscall gate (extracode э77)
        uj      syscall          // resumed?  halt again

        .globl  badext
badext: stop    076              // TODO: an extracode we do not implement
        uj      badext

// ============================================================================
// Trap and interrupt dispatch
// ============================================================================
//
// On x86 the exception, hardware-IRQ, and system-call vectors funnel through
// hand-written stubs that build a `struct trap` frame, honor `nofault` (the
// fault-recovery hook), call the C handler (trap() / the device ISR from the
// interrupt table), replay soft-deferred interrupts, and check `runrun` for a
// deferred reschedule.
//
// TODO: implement the BESM-6 trap/interrupt entry.  It must:
//   * save the interrupted context into a frame the C `trap()` can index
//     (mirror struct trap for the BESM-6 register set);
//   * dispatch CPU faults and the syscall extracode (the user-mode `$77 N`
//     trap, cf. cmd/sim) to trap()/the syscall path;
//   * provide the `nofault` fast-recovery path used by the user-access family;
//   * on return to user mode, honor `runrun` (reschedule) and profiling.

// ============================================================================
// External interrupt entry
// ============================================================================
//
// The hardware vector for an external (ГРП) interrupt is the fixed address 0501,
// laid down in the .const block above.  It jumps here; this stub saves what the C
// handler may clobber, calls extintr() in intr.c, and returns with `ij` (выпр).
//
// An interrupt is asynchronous, so the calling convention's callee-saved set is not
// enough -- the interrupted code owns every register.  extintr() preserves r1-r7 and
// r15 for us (that is the ABI) and the hardware has saved the interrupted PC in
// M[033], so what is left to save here is the accumulator and the caller-saved index
// registers r8-r14 -- including r13, which the `vjm` below is about to overwrite with
// its return address.
//
// Interrupts stay blocked throughout (the hardware sets БлПр on entry, and `ij`
// restores the mode word from СПРВ on the way out), so one static save area is safe.
//
// The same stub, and the reasoning behind it, is exercised standalone against SIMH
// by kernel/test/crt0.s + sctest.c.

        .globl  extint
extint: atx     sa               // save the accumulator
        ita     010              // ...and r8-r14, which the C call may clobber
        atx     s8
        ita     011
        atx     s9
        ita     012
        atx     s10
        ita     013
        atx     s11
        ita     014
        atx     s12
        ita     015
        atx     s13
        ita     016
        atx     s14

     13 vjm     extintr          // intr.c: read ГРП, dispatch, dismiss

        xta     s14
        ati     016
        xta     s13
        ati     015
        xta     s12
        ati     014
        xta     s11
        ati     013
        xta     s10
        ati     012
        xta     s9
        ati     011
        xta     s8
        ati     010
        xta     sa
      3 ij                       // выпр: restore the mode word, jump via M[033]

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
// separate floating-point register file -- the accumulator does float, and the
// arithmetic mode rides in СПСВ across a trap -- so there is no lazy FP context to
// save around a context switch, and nothing to restore.

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
// void bcopy(const void *src, void *dst, unsigned len) -- kernel block copy
// void bzero(void *dst, unsigned len)                  -- kernel block zero
//
// Plain kernel-to-kernel operations (no fault protection).  On x86 `len` is a byte
// count; on the word-addressed BESM-6 these are naturally word loops -- mind the
// six-chars-per-word packing when a byte count is not a word multiple.
//
// TODO: implement the copy / zero loops.

// void bcopy(src, dst, len)  -- three parameters (src, dst pushed; len in A).
        .globl  bcopy
bcopy:
        // TODO: copy `len` from src to dst.
        15 utm -2                // pop the two pushed arguments (src, dst)
        13 uj

// void bzero(dst, len)  -- two parameters (dst pushed; len in A).
        .globl  bzero
bzero:
        // TODO: zero `len` at dst.
        15 utm -1                // pop the one pushed argument (dst)
        13 uj

// ============================================================================
// Low-level device access
// ============================================================================
//
// int  inb(int addr);                    -- read a byte from an I/O port
// void outb(int addr, int value);        -- write a byte to an I/O port
// void insw (int addr, char *buf, int n);-- read n words port -> memory
// void outsw(int addr, char *buf, int n);-- write n words memory -> port
//
// These are x86 programmed-I/O primitives, used by the (x86) device drivers.  The
// BESM-6 has no x86 port space; on the real port these are replaced by the BESM-6
// channel / device-register access primitives, together with the driver layer.
//
// TODO: implement (or replace) once the BESM-6 device model exists.

// int inb(int addr)  -- one parameter (addr in A).
        .globl  inb
inb:
        xta                      // TODO: return the byte read
        13 uj

// void outb(int addr, int value)  -- two parameters (addr pushed; value in A).
        .globl  outb
outb:
        // TODO: write the byte.
        15 utm -1                // pop the pushed argument (addr)
        13 uj

// void insw(int addr, char *buf, int n)  -- three parameters.
        .globl  insw
insw:
        // TODO: read n words from the port into buf.
        15 utm -2                // pop the two pushed arguments (addr, buf)
        13 uj

// void outsw(int addr, char *buf, int n)  -- three parameters.
        .globl  outsw
outsw:
        // TODO: write n words from buf to the port.
        15 utm -2                // pop the two pushed arguments (addr, buf)
        13 uj

// ============================================================================
// Control registers, TLB, interrupt flag
// ============================================================================
//
// int  ld_cr0(void), ld_cr2(void), ld_cr3(void); -- read x86 control registers
// void invd(void);                                -- flush the TLB
// void cli(void);                                 -- disable interrupts
// void sti(void);                                 -- enable interrupts
//
// ld_cr0/2/3 are x86-only (used solely by the panic register dump in trap.c); they
// have no BESM-6 analogue beyond "read whatever status registers exist".  invd
// flushes the address-translation cache after page-table edits (utab.c); on BESM-6
// it is a no-op -- writing a page register refills the corresponding TLB entries in
// the same instruction, so a stale translation cannot happen (doc/Memory_Mapping.md).
// cli/sti are the global interrupt disable/enable around short critical sequences.
//
// TODO: map these onto the BESM-6 machine state; drop the ones with no meaning.

        .globl  ld_cr0
ld_cr0:
        xta                      // TODO: x86-only; no BESM-6 meaning
        13 uj

        .globl  ld_cr2
ld_cr2:
        xta                      // TODO: x86-only; no BESM-6 meaning
        13 uj

        .globl  ld_cr3
ld_cr3:
        xta                      // TODO: x86-only; no BESM-6 meaning
        13 uj

        // invd is gone: on the BESM-6 `reg 020+i' refills the TLB as it writes RP,
        // so a stale translation is not a state the machine can be in and there is
        // nothing for utab.c to invalidate (doc/Memory_Mapping.md).

        .globl  cli
cli:
        // TODO: disable interrupts globally.
        13 uj

        .globl  sti
sti:
        // TODO: enable interrupts globally.
        13 uj

// ============================================================================
// Global variables and data
// ============================================================================

        .bss

// Register save area for the external-interrupt stub above.  One copy is enough:
// interrupts are blocked from the moment the hardware takes one until the `ij` that
// ends the handler, so extint cannot be re-entered.
sa:     . = . + 1                // accumulator
s8:     . = . + 1                // r8-r14: caller-saved, so the C handler may
s9:     . = . + 1                //   clobber them and the interrupted code will
s10:    . = . + 1                //   not be expecting it
s11:    . = . + 1
s12:    . = . + 1
s13:    . = . + 1
s14:    . = . + 1

        .data

// int phymem -- physical memory size found by the boot memory scan (freed into
// coremap by startup()).
        .globl  phymem
phymem: .word 0                  // TODO: set during bring-up

// caddr_t waitloc -- address of the idle wait instruction; clock() compares the
// interrupted pc against it to charge idle time.
        .globl  waitloc
waitloc:.word 0                  // TODO: point at the idle-loop wait location

// int uhome -- the p_addr whose u-area is the one currently live at UBASE.  The
// live u-area is at 076000 and the copy in the process's image is stale between
// switches; resume() flushes and reloads when it finds uhome != the incoming
// p_addr.  See kernel/TODO.md, "The u-area invariant".
        .globl  uhome
uhome:  .word 0                  // TODO (task 14): set to proc[0].p_addr at boot

// struct user u -- the per-process user area: struct user at the bottom and the
// kernel stack growing up from it to 0100000.  It is not in the user's map and it
// is not in bss: it is the last page of the kernel's unmapped space, at a fixed
// PHYSICAL address, copied in and out of the process image on a context switch.
// So it is an absolute symbol, not storage -- b6as emits N_EXT|N_ABS and b6ld
// passes it through unrelocated.
        .globl  u
        u = 076000               // must agree with UBASE in include/sys/param.h
