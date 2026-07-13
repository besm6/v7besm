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
        . = . - 010 + 0100
#include "const.s"
        uj trap                  // 0500: internal interrupt (fault, extracode)
      : uj extint                // 0501: external interrupt (ГРП) -- see below

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
//   * BSS zeroed, `kend` set to the first free word after the kernel image;
//   * `phymem` set to the amount of physical memory found;
//   * the u-area and kernel stack mapped, interrupt vector installed;
//   * call main();  then enter user mode on the first process (icode).

        .text
        .globl  _start
_start:
        // TODO: BESM-6 processor initialization.
        // TODO: zero bss, set kend / phymem, map u-area + kernel stack.
        // TODO: install interrupt vector; call main; enter user mode.
     13 vjm main
        stop                        // placeholder


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
// int fubyte(caddr_t addr);            -- fetch one user byte,  -1 on fault
// int fuword(caddr_t addr);            -- fetch one user word,  -1 on fault
// int subyte(caddr_t addr, int value); -- store one user byte,  0 / -1
// int suword(caddr_t addr, int value); -- store one user word,  0 / -1
// int copyin (caddr_t from, caddr_t to, unsigned n); -- user->kernel, 0 / -1
// int copyout(caddr_t from, caddr_t to, unsigned n); -- kernel->user, 0 / -1
//
// These cross the user/kernel boundary safely: a bad user address must NOT panic
// the kernel.  Each validates the address range, arms the `nofault` recovery hook,
// performs the access, and disarms it; a fault during the access lands on a
// recovery path that returns -1.
//
// TODO: implement the BESM-6 versions.  Because the machine is word-addressed and
// has no sub-word load/store, the "byte" variants extract/insert a byte within a
// word (six chars per word).  All six need a `nofault`-equivalent expected-fault
// recovery cooperating with the trap handler.

// int fubyte(caddr_t addr)  -- one parameter (addr in A).
        .globl  fubyte
fubyte:
        xta                      // TODO: fetch user byte; return value or -1
        13 uj

// int fuword(caddr_t addr)  -- one parameter (addr in A).
        .globl  fuword
fuword:
        xta                      // TODO: fetch user word; return value or -1
        13 uj

// int subyte(caddr_t addr, int value)  -- two parameters (addr pushed, value in A).
        .globl  subyte
subyte:
        // TODO: store user byte; return 0 or -1.
        xta                      // return 0 (success placeholder)
        15 utm -1                // pop the pushed argument (addr)
        13 uj

// int suword(caddr_t addr, int value)  -- two parameters (addr pushed, value in A).
        .globl  suword
suword:
        // TODO: store user word; return 0 or -1.
        xta                      // return 0 (success placeholder)
        15 utm -1                // pop the pushed argument (addr)
        13 uj

// int copyin(caddr_t from, caddr_t to, unsigned n)  -- three parameters.
        .globl  copyin
copyin:
        // TODO: copy n bytes user->kernel with fault recovery; return 0 or -1.
        xta                      // return 0 (success placeholder)
        15 utm -2                // pop the two pushed arguments (from, to)
        13 uj

// int copyout(caddr_t from, caddr_t to, unsigned n)  -- three parameters.
        .globl  copyout
copyout:
        // TODO: copy n bytes kernel->user with fault recovery; return 0 or -1.
        xta                      // return 0 (success placeholder)
        15 utm -2                // pop the two pushed arguments (from, to)
        13 uj

// ============================================================================
// Floating-point state
// ============================================================================
//
// void savfp(void *ptr);   -- save the FP register/state image to *ptr
// void restfp(void *ptr);  -- restore the FP state from *ptr
// void stst(int *ptr);     -- store the FP status/exception word to *ptr
//
// Lazy FP management around context switches (keyed on u.u_fpsaved); stst captures
// the FP fault cause for the FP-error trap.
//
// TODO: the BESM-6 float format is not IEEE-754 and it has no x86-style FP state
// frame.  These become "save/restore whatever FP context must survive a switch"
// and "read the FP fault status", or near no-ops if FP faults are handled
// differently.

        .globl  savfp
savfp:
        // TODO: save FP state to the area pointed at by A.  One parameter.
        13 uj

        .globl  restfp
restfp:
        // TODO: restore FP state from the area pointed at by A.  One parameter.
        13 uj

        .globl  stst
stst:
        // TODO: store FP status word to the address in A.  One parameter.
        13 uj

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
// it becomes the MMU's translation-invalidate, or a no-op.  cli/sti are the global
// interrupt disable/enable around short critical sequences.
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

        .globl  invd
invd:
        // TODO: invalidate the address-translation cache (or no-op).
        13 uj

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

// int kend  -- first free word after the kernel image (set by _start; read by
// main()/startup() to size the initial process and free core).
        .globl  kend
kend:   .word 0                  // TODO: set during bring-up

// int phymem -- physical memory size found by the boot memory scan (freed into
// coremap by startup()).
        .globl  phymem
phymem: .word 0                  // TODO: set during bring-up

// caddr_t waitloc -- address of the idle wait instruction; clock() compares the
// interrupted pc against it to charge idle time.
        .globl  waitloc
waitloc:.word 0                  // TODO: point at the idle-loop wait location

        .bss

// struct user u -- the per-process user area (kernel stack + per-process state),
// referenced pervasively by the C kernel.  On x86 it is mapped per process at a
// fixed virtual address (USIZE = 2 pages).  Here it is reserved in bss so the link
// resolves.
//
// TODO: implement the real per-process u-area mapping (swap in/out with the
// process image).  The reservation below is a placeholder sized generously to hold
// struct user; adjust to sizeof(struct user) in words once the layout is fixed.
        .globl  u
u:      . = . + 512              // reserve 512 words (placeholder for struct user)

// int pdir[]  -- page directory (indexed by physaddr() in utab.c).
// int upt[]   -- user page table (rewritten by sureg() in utab.c; indexed to 1023).
// mem         -- base of the window mapping physical memory into kernel space (PHY).
//
// TODO: these model the x86 paging structures; replace with the BESM-6 memory map.
// Reserved here as placeholder arrays so utab.c links.
        .globl  pdir
pdir:   . = . + 1024             // reserve 1024 words (placeholder page directory)

        .globl  upt
upt:    . = . + 1024             // reserve 1024 words (placeholder user page table)

        .globl  mem
mem:    . = . + 1                // placeholder memory-window symbol
