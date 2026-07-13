//
// crt0.s -- bare-metal startup and interrupt vectors for the standalone SIMH
// tests in this directory.
//
// The libc crt0 (~/.local/share/besm6/lib/crt0.o) cannot be used here.  It ends by
// calling exit through the extracode `$77 1`, which is a *syscall*: b6sim services
// it, but under SIMH there is no kernel underneath, so the extracode would vector
// into the constant pool at 0577.  It also never seeds r15 -- b6sim's loader seeds
// the stack pointer, and SIMH does not.
//
// So: seed r15, let external interrupts in, call main(), and halt.  `stop` is
// resumable on the real machine, hence the endless loop around it: if execution
// ever continues past the halt, we halt again rather than run off into whatever
// follows.  main()'s status is left in the accumulator, where the .ini asserts on it.
//
// This is the rehearsal for kernel/besm6.S:_start and its interrupt dispatch, which
// have the same holes.
//

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------
// The hardware vectors are two fixed low addresses: 0500 for an internal interrupt
// (a fault, and every extracode: 0500 + opcode) and 0501 for an external one (ГРП).
// The const segment is based at 010, so `. - 010 + 0500` puts the location counter
// exactly on 0500.  A lone `:` aligns to the next word, so the two `uj`s are the
// left instructions of words 0500 and 0501, which is what the hardware jumps to.

        .const
        . = . - 010 + 0500
        uj      fault           // 0500: internal interrupt
      : uj      extint          // 0501: external interrupt (ГРП)

        .text
        .globl  _start
_start:
     15 vtm     stack           // seed the C stack pointer; nothing else will
        vtm     3               // PSW := БлП|БлЗ.  Writing PSW is what index
                                //   register 0 means for `vtm` in supervisor mode;
                                //   the point is the bit it does NOT set, БлПр
                                //   (02000), which reset leaves on and which blocks
                                //   every external interrupt.
     13 vjm     main            // main() -- its status stays in the accumulator
halt:   stop
        uj      halt            // resumed?  halt again

// An internal interrupt means we faulted.  Halt with a recognizable code so the
// .ini can tell a fault from a clean exit by the halt PC.
fault:  stop    07777
        uj      fault

// ----------------------------------------------------------------------------
// extint -- external interrupt entry, calls extintr() in C
// ----------------------------------------------------------------------------
// An interrupt is asynchronous, so the C calling convention's callee-saved set is
// not enough: the interrupted code owns every register.  extintr() preserves r1-r7
// and r15 for us (that is the ABI), and the hardware saved the PC in r13' = M[033].
// What is left for this stub to save is the accumulator and the caller-saved index
// registers r8-r14 -- including r13, which the `vjm` below is about to overwrite
// with its return address.
//
// Interrupts stay disabled throughout (the hardware sets БлПр on entry and `ij`
// restores it from СПРВ), so a single static save area is safe.

extint: atx     sa              // save the accumulator
        ita     010             // ...and r8-r14, which the C call may clobber
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

     13 vjm     extintr

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
      3 ij                      // выпр: restore the PSW from СПРВ, jump via M[033]

        .bss
sa:     . = . + 1               // accumulator save slot
s8:     . = . + 1
s9:     . = . + 1
s10:    . = . + 1
s11:    . = . + 1
s12:    . = . + 1
s13:    . = . + 1
s14:    . = . + 1

// The stack grows toward higher addresses, so r15 starts at the low end.
stack:  . = . + 512
