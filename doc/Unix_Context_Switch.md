# How Unix on the BESM-6 saves and restores the CPU context

This is the same question [Dubna_Context_Switch.md](Dubna_Context_Switch.md) asks of Dubna, asked of
**this** kernel: how Unix v7 on the BESM-6 takes an interrupt, takes an extracode, saves the CPU
context, switches address spaces, and gets back out again. Every claim below is quoted from the tree
— [`kernel/besm6.S`](../kernel/besm6.S), [`kernel/switch.s`](../kernel/switch.s),
[`kernel/trap.c`](../kernel/trap.c), [`kernel/syscall.c`](../kernel/syscall.c),
[`kernel/utab.c`](../kernel/utab.c), [`kernel/uarea.s`](../kernel/uarea.s),
[`include/sys/reg.h`](../include/sys/reg.h) — and all of it is code that runs, exercised against the
real SIMH machine by the standalone tests in [`kernel/test/`](../kernel/test/) (§13).

Three documents divide this territory, and the split is sharp:

- **[Memory_Mapping.md](Memory_Mapping.md)** says what the *hardware* does at a trap: what goes into
  SPSW, what `выпр` restores, which mode bits are forced.
- **[Dubna_Context_Switch.md](Dubna_Context_Switch.md)** says what a kernel that *worked on the real
  machine* did about it. Several idioms below are taken from it outright, and are credited where
  they appear.
- **This** says what our kernel does, and why it differs where it differs. §14 is the summary of
  those differences.

[Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md) is the fourth: it specifies each assembly
routine's *contract* with its C callers, written against the x86 original as the port's spec. That
document says what each routine must do; this one walks the path they form.

> **Octal and bit numbering.** Most addresses below are **octal**. BESM-6 numbers bits
> **right-to-left starting at 1**, so bit 1 is the least significant and bit 48 the most significant
> — see [Besm6_Data_Representation.md](Besm6_Data_Representation.md).

---

## Table of contents

- [How Unix on the BESM-6 saves and restores the CPU context](#how-unix-on-the-besm-6-saves-and-restores-the-cpu-context)
  - [Table of contents](#table-of-contents)
  - [1. What the hardware hands us, and what it does not](#1-what-the-hardware-hands-us-and-what-it-does-not)
  - [2. The vector block](#2-the-vector-block)
  - [3. The trap frame](#3-the-trap-frame)
  - [4. The five temp cells, and why only five](#4-the-five-temp-cells-and-why-only-five)
  - [5. The stack switch](#5-the-stack-switch)
    - [`intrframe` — a conditional frame base has to be published](#intrframe--a-conditional-frame-base-has-to-be-published)
  - [6. The fill](#6-the-fill)
  - [7. A fault, in C](#7-a-fault-in-c)
    - [The PC fixup](#the-pc-fixup)
    - [Cause decode and dismissal](#cause-decode-and-dismissal)
    - [Dispatch](#dispatch)
  - [8. An extracode, in C](#8-an-extracode-in-c)
    - [Latching the call number](#latching-the-call-number)
    - [Marshalling](#marshalling)
    - [Dispatch and return](#dispatch-and-return)
    - [The caller-side hazard: an extracode in a left half](#the-caller-side-hazard-an-extracode-in-a-left-half)
  - [9. An interrupt, in C](#9-an-interrupt-in-c)
  - [10. The exit](#10-the-exit)
    - [Why there is no `OUTMACRO`](#why-there-is-no-outmacro)
  - [11. Switching address spaces](#11-switching-address-spaces)
    - [The БРЗ drain](#the-брз-drain)
  - [12. Switching processes](#12-switching-processes)
    - [`save()` — a BESM-6 `setjmp`](#save--a-besm-6-setjmp)
    - [`resume()` switches the u-area, not the address space](#resume-switches-the-u-area-not-the-address-space)
    - [The u-area invariant](#the-u-area-invariant)
    - [`swtch()`](#swtch)
  - [13. How it is verified](#13-how-it-is-verified)
  - [14. Where this differs from Dubna](#14-where-this-differs-from-dubna)

---

## 1. What the hardware hands us, and what it does not

A trap on this machine is cheap and nearly empty. The hardware:

- forces **БлП, БлЗ and БлПр on** — so the handler runs unmapped, with data protection off and
  external interrupts blocked;
- saves the old mode bits, plus the supervisor bits, in **SPSW** (`M[027]` — the register
  [Memory_Mapping.md](Memory_Mapping.md) and the Russian sources call СПСВ);
- deposits the return PC in **IRET** (`M[033]`) for an interrupt or fault, or **ERET** (`M[032]`)
  for an extracode;
- for an extracode, additionally hands the **effective address in `M[14]`**;
- vectors to a fixed word address (§2).

That is all of it. The full hardware account is [Memory_Mapping.md](Memory_Mapping.md), "Entering and
leaving supervisor mode". What matters here is the complement — **what nothing saves**, because
every one of those is a bug the kernel has to close by hand:

| Not saved | Why it bites |
|---|---|
| **R**, the mode register (ω + the NTR suppress bits) | The C ABI exits `NTR 3` / ω = logical ([Besm6_Runtime_Library.md](Besm6_Runtime_Library.md)), so the C handler *always* returns with R changed. Resume the user with it and its next floating-point instruction quietly does something else. `выпр` does not restore R; software must. |
| **Y** (РМР), the younger-bits register | Every logical op (`и`, `слц`, `сл`) overwrites it. The interrupted code may hold it live — mid-multiply, or between a `счмр` and its use. |
| **М15**, the stack pointer | **Not banked by mode.** There is one stack register across user and supervisor, so a trap from user leaves М15 naming a *user* address while БлП has just been forced on — see §5, this is the sharpest edge in the file. |
| **`M[16]`**, the C register (address modifier) | Its *value* stays in the register file, inert, but the C handler will overwrite it — the compiler's idiom for a global is `utc name` + a bare load. See below. |

The last one deserves its own paragraph, because the hardware does half the work and the half it
does is easy to mistake for all of it. A pending `utc`/`wtc` — armed for exactly one instruction —
has **two pieces of state in two places**. At the trap, the hardware migrates the armed flag into
SPSW (`SPSW_MOD_RK`) and *disarms* the live modifier, leaving the **value** sitting untouched in
`M[16]`; the closing `выпр` reconstructs the pending modification from the two.

So the armed bit rides in SPSW for free — but only if the kernel preserves the value. A device
interrupt can land in the one-instruction window between a user `utc` and the instruction it
modifies; if `extintr()` clobbers `M[16]` in the meantime, `выпр` re-arms from the *wrong* address.
Nothing faults. A load just reads the wrong word. [Dubna_Context_Switch.md](Dubna_Context_Switch.md)
§13 is the full anatomy; the fix here is one save and one restore (§4, §10).

## 2. The vector block

The vectors are laid out **by absolute address** in the `.const` segment of
[`kernel/besm6.S`](../kernel/besm6.S). Unlike Dubna — which had no origin directive and counted words
by hand — our assembler has `.org` and enforces the placement:

```
        .const
        .org 0100               // Skip I/O region
#include "const.s"              // Put some #constants here to fill the space

        .org 0500               // Vectors for exceptions and interrupts are at 0500
      : uj trapgate             // 0500: internal interrupt (a fault) -- see below
      : uj intrgate             // 0501: external interrupt (ГРП) -- see below

        .org 0550               // Vectors for extracodes start at 0550
      : uj badext               // 0550: extracode *50
      ...
      : uj badext               // 0576: extracode *76
      : uj sysgate              // 0577: extracode *77 -- the syscall gate
```

The origins hold only as long as the const segment really begins at `BADDR == 010`
(`cmd/ld/intern.h`), which is what `.org` assumes: **`besm6.o` must come first in the link** (see
`OBJ` in `kernel/Makefile`) so nothing is merged in ahead of it, and `ld -T` must not be used.

**Four doors, where Dubna has three** — because the syscall extracode and the catch-all for every
other extracode are separate vector words, and the hardware identifies an extracode purely by *which
word it lands on*:

| Gate | Vector | C handler | Discipline |
|---|---|---|---|
| `trapgate` | `0500`, internal fault | `trap()`, `trap.c` | full save; **unconditional** stack switch; PC fixup in C |
| `intrgate` | `0501`, external (ГРП) | `extintr()`, `intr.c` | full save; **conditional** switch; publishes `intrframe` |
| `sysgate` | `0577` (э77) | `syscall()`, `syscall.c` | full save; unconditional switch; no PC fixup |
| `badext` | `0550`–`0576` | `badextr()`, `syscall.c` | as `sysgate`; posts SIGINS |

Note the aliases: **э20/э60 and э21/э61 share one vector word each** (the hardware maps `э20`/`э21`
to `0540 + (opcode >> 3)`), and a user `стоп` arrives as **э63**.

`badext` and `sysgate` are duplicated bodies rather than one body behind a discriminator. That is
deliberate: nothing before the frame is filled can tell the doors apart, so sharing would cost a flag
cell and a branch to save a block that is otherwise identical. **The copies must be kept in step.**

## 3. The trap frame

Every gate builds the same 21-word frame on the kernel stack, declared once in
[`include/sys/reg.h`](../include/sys/reg.h):

```
  0  ACC   accumulator (primary syscall result)
  1  RREG  R  -- ALU mode word (ω + the NTR suppress bits)
  2  RMR   Y (РМР) -- younger-bits register
  3  RET   return address: IRET (И33) for a fault, ERET (И32) for an extracode
  4  SPSW  saved mode word (М027)
  5  CREG  М16 = C register M[16] (M[020]), the address modifier
  6  R15   stack pointer
  7  R14   argument-count / errno register
  8..20    М13 down to М1
```

The general registers are stored **descending**, М15..М1 at offsets 6..20 — the shape the `its` fill
pipeline produces (§6), and the same shape Dubna's ИПЗ has for the same reason. `struct trap` in the
same header names the words for the C side.

Three properties of the frame are load-bearing:

**One RET slot, not two.** IRET and ERET are the *only* structural difference between the
asynchronous doors and the synchronous ones. Each door's fill reads the return register its own door
uses — `its IRET` or `its ERET` — into the frame's single `RET` slot, and the shared epilogue reloads
that slot into IRET and leaves through one hardcoded `3 ij` for all four. §10 explains why that is
correct.

**It lives at a link-time constant.** The kernel stack is the top of `struct user`, and
`ustkbase` (`machdep.c`) is `&u.u_stack[0]` — with the u-area a fixed physical page at `076000`, that
is ≈ `076214`. `_start` seeds М15 with it, and `trapgate`/`sysgate`/`badext` reload it on every entry.

**It is aliased in place, not copied.** `trap()` and `syscall()` reach it as
`(struct trap *)u.u_stack` and point `u.u_ar0` at it. So a register that `trap()`, `psig()` or
`sendsig()` writes through `u.u_ar0` **is** the register the epilogue reloads on the way out. That
is what makes signal delivery and `exec()`'s "start at the new entry point" work without any separate
mechanism.

Two C-side conveniences ride on the frame. `USERMODE(spsw)` tests `SPSW & 014` (РежЭ | РежПр), both
clear iff the interrupted context was user — this replaces the x86 CS-ring test. And `BASEPRI(x)` is
**permanently 0**, which is a fact about the hardware, not a stub: this kernel has two interrupt
levels rather than the PDP-11's eight (§9), so anything `clock()` interrupts was by construction
running at base priority.

## 4. The five temp cells, and why only five

```
save_a:  .word 0                // A
save_r:  .word 0                // R   -- ALU mode (ω + the NTR suppress bits)
save_y:  .word 0                // Y (РМР) -- younger-bits register
save_c:  .word 0                // M16 -- the C register / address modifier
save_sp: .word 0                // interrupted M15 (the stack pointer)
```

**Why only five.** The fill pipeline (§6) runs *before* the C call, so М1–М14 and IRET/SPSW are still
live and go straight into the frame. The five above are exactly the registers the pipeline cannot
reach: A (it *is* the pipeline register), R and Y (only `rte`/`yta` read them, and both destroy A),
`M[16]` (must be spilled before the stack switch), and the interrupted М15 (the pipeline's own frame
pointer, about to be repointed).

**Why one copy serves every gate.** БлПр is held from the vector to the closing `выпр`, and the
kernel runs unmapped, so no external interrupt and no fault can land inside a gate's live window. The
gates cannot re-enter themselves and cannot preempt each other. (A second internal fault taken inside
a gate is fatal regardless — SPSW and IRET are single registers, not a stack.)

**Why they live in `.text`.** They link below `07777`, so the gate reaches them with a bare 12-bit
`atx save_a`. This is not a nicety: a `< sym >` escape emits a `мода`/`utc` that loads `M[16]`, so an
escape ahead of the `M[16]` save would overwrite the very C register being saved. The kernel's own
bss links far above `07777` and needs the escape; these cells must not.

**The order is a contract, not a style choice.** Two constraints fix it:

- **`rte` must precede `yta`.** `yta` copies Y into A *only in ω = logical*; in any other ω it merges
  Y into A's mantissa and adjusts the exponent instead. The interrupted ω is the **user's** and can
  be anything, so something must force logical first — and `rte` does it as a side effect (it reads
  R into A's exponent field and *then* sets ω = logical). Nothing that changes ω may be inserted
  between the two.
- **The `M[16]` spill must precede the stack switch.** `[ustkbase]` assembles to a `wtc`, which loads
  `M[16]`; so does a label's alignment filler in text. Anything that reaches `M[16]` after the spill
  is harmless; anything before it would frame the wrong value.

## 5. The stack switch

This is the one place where the BESM-6 forces a design the PDP-11 v7 never needed. On the PDP-11, SP
is banked by processor mode, so a trap from user lands with SP already naming the per-process kernel
stack — the switch is free. **The BESM-6 has one stack register, М15, shared across modes.**

So when the interrupt lands in user code, М15 holds the user stack pointer (`exec` seeds it at
`070000`, growing up) — and the trap has just forced **БлП on**, so supervisor data is unmapped. That
value is now a **physical** word index at ≈ `070000`, *inside the kernel image, below the u-area at
`076000`*. The C handler's prologue would write its frame straight into the kernel's own text and
data. Silent corruption, invisible until a device ISR is actually armed.

`trapgate` and `sysgate` switch **unconditionally**:

```
     15 vtm     [ustkbase]          // M15 := kernel stack base (~076214) = frame base F
```

An extracode only ever comes from user (the kernel never issues one), and a *fault* from supervisor
is a kernel bug — the user-access family validates its range up front with `useracc()` and there is no
`nofault` path anywhere, so `trap()` takes such a fault to the `default` arm and panics. Resetting
М15 under a panic is harmless. Neither door nests; the frame is always at the link-time constant, and
neither gate passes anything to C.

`intrgate` is the exception, because an external interrupt legitimately nests inside the kernel and
must not have the stack pulled out from under an interrupted C frame:

```
        ita     SPSW                // SPSW -> A
        aax     #(SPSW_INTERRUPT | SPSW_EXTRACODE)
        u1a     extk                // РежЭ|РежПр set -> nested in the kernel: keep M15
     15 vtm     [ustkbase]          // zero -> from user: M15 := kernel stack base (~076214)
extk:                               // M15 = frame base F (ustkbase, or the interrupted kernel SP)
```

**Test the supervisor bits, not БлП.** `copyin`/`copyout` clear БлП while staying in supervisor mode,
so a БлП test would misclassify a fault taken mid-`copyin` and reset М15 out from under the syscall
frame.

### `intrframe` — a conditional frame base has to be published

Because the switch is conditional, `intrgate`'s frame base is a **run-time** value, not something the
C side can spell. So the gate publishes it, in two instructions:

```
        ita     15                  // A := F -- the `aax' result it overwrites is dead here
        atx     intrframe           // a plain cell, bare-addressed: no escape, no M16 touched
```

and `clock()` reads it as `clock((struct trap *)intrframe)`.

It has to be a **private cell, not `u.u_ar0`** — which was the obvious candidate, being v7's own name
for the current register frame. Three reasons it is wrong here:

- `clock()` cannot use `u.u_stack` at all: on a tick nested inside a syscall, that holds the
  *syscall's* frame, whose SPSW says "user" — so the tick would be charged to user time and profiled
  at a stale user PC.
- The store would have to be **unconditional** (from the kernel too, or `clock()` reads the wrong
  frame in exactly the nested case that motivates all this), so it would transiently overwrite the
  `u_ar0` of an interrupted syscall — and `exec()` (`sys1.c`) and `sendsig()` (`machdep.c`) write the
  resumed PC and the signal frame *through* `u_ar0`, from paths that sleep. A tick landing between
  the assignment and the use would send those writes into a frame that is dead by the time anything
  resumes.
- `u_ar0` means the **user's** saved registers. A tick nested in the kernel frames a *kernel*
  context; pointing `u_ar0` at it is a small lie even for the length of one call.

One cell suffices for the same reason one `save_a` does: БлПр is held throughout, so the gate cannot
re-enter itself.

## 6. The fill

This is Dubna's `FULSAV` pipeline ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §6), taken
outright. `xts`/`its` each **store the accumulator and load the next value into it** in one
instruction, so the whole frame is filled with one instruction per word, no scratch cell and no loop
overhead. The accumulator is the pipeline register; М15 walks `F → F+21`.

```
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
        ...
        its     1                   // frame[19] := M2    ; A := M1
        xts     save_a              // frame[20] := M1    ; M15 := F+21; A discarded
```

Its real value here turned out to be a **side effect** rather than the instruction count: because the
pipeline's only scratch is the accumulator, every register it reaches is captured **live**, before
`13 vjm <handler>` can clobber anything. That is what keeps the temp-cell list down to five (§4), and
it is also what makes the door-merge free:

> **`sysgate` is `trapgate` with exactly one instruction changed** — `its ERET` where the fault gate
> reads `its IRET`. Since the fill already reads a return register live, it simply reads the other
> one.

`badext` is a third verbatim copy of the same block.

## 7. A fault, in C

`trap()` ([`kernel/trap.c`](../kernel/trap.c)) is entered with the frame complete and no arguments.
It finds the frame for itself at the link-time constant, and aliases it:

```c
    register struct trap *tr = (struct trap *)u.u_stack;
    ...
    u.u_ar0 = (int *)tr;
```

**The trap kinds are ours, not the hardware's.** The BESM-6 has one internal-interrupt vector and
reports the cause in ГРП, so unlike the x86 there is no vector number to switch on and the gate
passes none:

```c
#define T_DATA  1 /* data protection: touched a closed page */
#define T_INSN  2 /* instruction protection: fetched from a closed page */
#define T_ILL   3 /* privileged instruction attempted in user mode */
#define T_CHECK 4 /* instruction check: the word fetched is not an instruction */
#define T_BREAK 5 /* address-break match (М034/М035) */
```

There is deliberately **no `T_SYSCALL`**: an extracode is not reachable through `0500` at all (§2).

### The PC fixup

Before vectoring, the machine advances past the faulting instruction and records that it did so in
SPSW. So the saved IRET points at the instruction *after* the one that faulted, and a plain `выпр`
would skip it. Undoing that is the kernel's job, and it is done in C — two lines, because the frame is
aliased in place:

```c
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--;
        tr->spsw &= ~SPSW_NEXT_RK;
    }
```

Only the *word* is off, and always by one: `SPSW_RIGHT_INSTR` already names which half of the word
faulted, and it is left alone. The derivation is [Memory_Mapping.md](Memory_Mapping.md), "The restart
protocol"; `kernel/test/utrap` verifies it from **both** instruction halves.

### Cause decode and dismissal

The cause is read live out of ГРП — the fault bits are not framed — and the offending page comes from
`(grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT` for data violations. Dismissal uses `MOD_GRPCLR`, which
clears the bits that are **zero** in the accumulator (hence the `~GRP_OPRND_PROT` spelling).

This is Dubna's internal/external asymmetry, taken
([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §5): a fault is **not queued** — if the condition
persists the next instruction re-raises it — so clearing it outright is safe, and necessary, or a
handled fault bit fires afterwards as a spurious external interrupt. A device interrupt, by contrast,
is a one-shot notification and only the dispatched bit is cleared (§9).

### Dispatch

`dev |= USER` if `USERMODE(tr->spsw)`, then:

- **`T_DATA` from user** → `grow()` the stack and retry. No signal — this is the normal
  stack-growth path.
- **`T_INSN` from user** → SIGSEG. **`T_ILL`/`T_CHECK`** → SIGINS. **`T_BREAK`** → SIGTRC.
- **anything from supervisor** → the `default` arm: dump `acc`/`r13`/`r14`/`r15`, `ret`/`spsw`/`grp`/
  `page`, `R`/`RMR`/`C`, and panic. Per §5, a kernel-mode fault is a kernel bug.

The tail is shared with the syscall path: `issig()`/`psig()`, `curpri = setpri()`,
`if (runrun) qswtch()`, `addupc()`.

## 8. An extracode, in C

`syscall()` ([`kernel/syscall.c`](../kernel/syscall.c)) finds the same frame the same way. The file is
split out of `trap.c` for one reason: `kernel/test/usys` links the **real** dispatcher without
dragging in printf, signals and `grow()`.

**No PC fixup**, unlike `trap()` — the extracode gate stores `nextpc` in ERET, so `SPSW_NEXT_RK` is
never set by this door and there is nothing to back up.

### Latching the call number

```c
    code  = (unsigned)tr->r14;
    callp = (code < NSYSENT) ? &sysent[code] : &badsysent;
```

Two decisions in two lines. **Latch now**, because `R_ERRNO` is that same slot and the return path is
about to write it. And **range-check rather than mask**: the user can index-modify the effective
address to any 15-bit value, and a mask would fold an out-of-range number onto a real syscall.
`badsysent` routes to `nosys()`.

Note that r14's *other* ABI role — the negative argument count — is deliberately not read. The count
comes from `sysent[]`.

### Marshalling

The BESM-6 convention pushes arguments in direct order and leaves the **last one in the accumulator**;
v7 wants them ascending in `u_arg[]`. So the dispatcher inverts the layout:

```c
    n = callp->sy_narg;
    if (n > 0) {
        ap = (int *)tr->r15 - (n - 1);
        for (i = 0; i < n - 1; i++)
            u.u_arg[i] = fuword((caddr_t)ap++);
        u.u_arg[n - 1] = tr->acc;
    }
```

They live in **user space**, so this is `fuword()`, not a kernel dereference. The gate then owes the
callee's stack cleanup — and it is done *before* the dispatch, because `exec()` reseeds r15 and
`sendsig()` builds its frame on the user stack.

### Dispatch and return

```c
    if (save(u.u_qsav)) {
        /* the EINTR path: a signal longjmp'd back here out of sleep() */
        if (u.u_error == 0)
            u.u_error = EINTR;
    } else {
        (*callp->sy_call)();
    }
```

That `save(u.u_qsav)` is the landing pad `sleep()` longjmps back to when a signal interrupts it (§12).

The return writes the frame directly. **There is no carry flag on this machine**, so the error
convention is errno-in-r14, zero on success:

```c
    if (u.u_error) {
        tr->acc = -1;
        tr->r14 = u.u_error;
    } else {
        tr->acc = u.u_r.r_val1;
        tr->r12 = u.u_r.r_val2;
        tr->r14 = 0;
    }
```

`badextr()` — every extracode э50–э76 — does none of this: it posts SIGINS and falls into the same
shared tail (`issig`/`psig`, `setpri`, `qswtch` on `runrun`, `addupc`).

Two v7 idioms disappeared in the port. `fork()` tells parent from child by `r_val2` (r12) rather than
the x86 `u.u_ar0[RET] += 2` trick — RET is a *word* address and there is nothing to skip. And `exec`
sets `u.u_ar0[RET] = u.u_exdata.ux_entloc`: exec is an extracode, so the new image starts via `выпр`
through the frame's RET slot.

### The caller-side hazard: an extracode in a left half

The hardware returns an extracode to the **left half of the next word**, and saves no
right-instruction indicator. So the right half of the extracode's own word is *never executed*
([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §9). A syscall stub written as

```
putch:  $77 4           // LEFT half
     13 uj              // RIGHT half -- LOST
```

falls straight through its own return. This is not something the gate can repair — the half is not
recorded anywhere for it to find — so it is a constraint on every caller. `kernel/test/usys`'s
`uprog` carries a `10 utm 0` no-op before each extracode for exactly this reason.

## 9. An interrupt, in C

`extintr()` (`kernel/intr.c`) reads ГРП, dispatches to the device, and dismisses **only the bit it
handled** — the other half of the asymmetry in §7, because a device interrupt is a one-shot
notification that must not be lost.

**`spl` is БлПр, not МГРП.** This kernel has exactly two interrupt levels, not the PDP-11's eight
(which is why `BASEPRI()` is permanently false, §3). `setipl()` calls `cli()`/`sti()` in
`kernel/psw.s`, a read-modify-write of ПСВ bit `02000` — never a `vtm`, because `vtm N,0` writes БлП,
БлЗ *and* БлПр together. МГРП is armed once by `intrinit()` and never rewritten.

The two used to be the other way round, and they fought: `spl0()` opened МГРП while БлПр still
blocked everything, so no interrupt could be taken in kernel mode at all. That became load-bearing
the moment `idle()` needed to spin waiting for one.

**`idle()` is ordinary C**, not assembly, and it is a spin rather than a wait — there is no
wait-for-interrupt on this machine, and the only halt is `033` («стоп»), which every simulator treats
as "the run is over". So it spins at spl0 on a `volatile int idling` that `extintr()` clears after
servicing anything at all. The same flag replaces x86's `waitloc` for `clock()`'s idle accounting,
because a PC comparison would have to be calibrated against what the hardware saves.

`clock()` takes its frame from `intrframe`, for the reasons in §5.

## 10. The exit

One epilogue, `intret`, serves all four doors. It is the fill run backwards: `stx`/`sti` pop
`frame[20..0]` with М15 walking `F+21 → F`.

```
intret:
        stx                         // A := M1 from frame[20]
        sti     1                   // restore M1; A := M2 from frame[19]
        ...
        sti     14                  // restore M14;  A := M15 from frame[6]
        stx     save_sp             // prepare M15;  A := M16 from frame[5]
        sti     MOD                 // restore C;    A := SPSW from frame[4]
        sti     SPSW                // restore SPSW; A := RET from frame[3]
        sti     IRET                // restore RET into М033 (IRET);  A := Y from frame[2]
        stx     save_y              // prepare Y;    A := R from frame[1]
        stx     save_r              // prepare R;    A := A from frame[0]
        atx     save_a              // prepare A
        xta     save_y              // Y back, via the `aex` side effect
        aex                         //   (A is garbage after this -- intentional)
        xta     save_sp             // interrupted M15 back
        ati     15
        xta     save_a              // A back (xta does not disturb Y)
        xtr     save_r              // R back -- must be last
      3 ij                          // return from interrupt via M[033]=IRET
```

Its precondition is М15 = `F+21` and A dead — which is exactly where a C call with no parameters
leaves every gate, since a callee with no parameters returns М15 unchanged
([Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)).

**Four details are not obvious**, and all four are Dubna's ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §7):

- **The blank-address `aex` is not a XOR.** It XORs A with word 0, which always reads 0, so A is
  unchanged. The instruction exists *solely* for its side effect: a logical operation copies the old
  A into Y. `xta save_y; aex` is therefore "Y := the saved Y" — and it is the only way to write that
  register, since the architecture provides `счмр` to read it and nothing at all to write it.
- **The restore order Y → A → R is forced.** `xta` does not clear Y (unlike `и`/`слц`/`сл`), so A can
  be reloaded after Y without destroying it — any other order does. And `xtr` must be last, because a
  subsequent `xta` would perturb the mode bits it just restored.
- **`sti MOD` does not arm the modifier.** Only `utc`/`wtc` arm; `ita`/`ati`/`sti` move register 16
  as a plain value. So restoring the C register's *value* is all that is needed — the *pending* state
  rides in SPSW's `SPSW_MOD_RK` and is re-established by `выпр` (§1).
- **The М1–М7 reload is redundant** — they are ABI callee-saved — but it keeps the pop symmetric.

### Why there is no `OUTMACRO`

Dubna's best idea is a two-instruction door-merge: `OUTMACRO` copies ERET into IRET so that one
hardcoded `выпр` serves both doors ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §8). It was
recommended to this port, and **it turned out not to be needed.**

Because the fill already reads a return register live, the syscall gate simply reads *the other one*
(§6): one instruction, at entry rather than exit, and `intret` is reused completely unmodified. Its
`sti IRET` loads the extracode's return address into `M[033]` and its `3 ij` returns through it —
which is correct, because **the `ij` index field selects only which register holds the PC**, while the
mode, and so user-vs-supervisor, comes from SPSW either way.

Dubna's predicted bonus arrived anyway: the syscall return inherits the interrupt epilogue's `runrun`
and pending-signal checks for free.

## 11. Switching address spaces

**A trap switches nothing.** РП always holds the current process's map, and the kernel runs unmapped
(БлП = БлЗ = 1), so a kernel address *is* a physical address. This is why `copyin`/`copyout` are a
matter of clearing one bit rather than switching an address space — `kernel/usermem.s` toggles БлП per
word around each user access, the same bracket Dubna uses
([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §8).

The map is eight write-only page registers РП (`002 020`–`027`) plus the protection register РЗ
(`002 030`–`033`), covering 32 pages of 1 Kword. **Neither can be read back**, so `u.u_upt[8]` in the
u-area is the only copy of the mapping there is. The packing is chosen so nothing has to be shifted:
РП uses accumulator bits 1–20 and 29–48, РЗ uses bits 21–28, so **one word carries four page
descriptors *and*, in the even words, the protection byte of eight pages.**

`sureg()` ([`kernel/utab.c`](../kernel/utab.c)) rebuilds that shadow from `u_tsize`/`u_dsize`/
`u_ssize` — text from virtual page 0, data at `p_addr + USIZE`, stack at `USTKPAGE` (28, virtual
`070000`) growing up — and then loads the whole address space in twelve `рег`s:

```c
    drainbrz();
    for (i = 0; i < 8; i++)
        __besm6_mod(020 + i, u.u_upt[i]);
    for (i = 0; i < 4; i++)
        __besm6_mod(030 + i, u.u_upt[2 * i]);
```

Dubna's `PUTTMP` does the same job in the same twelve writes, for the same reason, arrived at
independently ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §10).

Two notes on the shadow build. **Text is left open to data**: the machine has no read-only page, and
closing the text page would take the constant pool with it. And `estabur(nt, nd, ns, sep, xrw)` only
validates sizes and calls `sureg()` — `xrw` and `sep` are accepted and ignored, since there is no
read-only page and no I/D separation.

### The БРЗ drain

`drainbrz()` ([`kernel/brz.s`](../kernel/brz.s)) is nine consecutive stores to physical 1–7:

```
drainbrz:
        atx     1                // nine consecutive stores to physical 1-7 --
        atx     2                //   nothing may come between them
        atx     3
        atx     4
        atx     5
        atx     6
        atx     7
        atx     1
        atx     2
     13 uj
```

Nine, not eight: the first store only arms the counter, and eviction begins with the second. A dirty
line carries a **virtual** tag, so a kernel that reloads РП with dirty lines outstanding writes the
old process's stores into the *new* process's memory.

**It cannot be written in C**, and this is verified by disassembly, not asserted: `b6cc` materializes
the destination pointer through a frame slot, so each C store emits two ordinary stores of its own and
resets the flush counter. Dubna wrote the identical nine-store loop
([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §11), which is about as strong an independent
confirmation as this port is going to get.

Call sites: before every РП write in `sureg()`, and **twice each** inside `uflush`/`uload`
(`kernel/uarea.s`) — before stealing the window (the kernel's own stores were tagged physical, so the
mapped copy would miss them) and after the copy (whose stores were tagged virtual and must go out
while the window is still installed).

> **The hazard is invisible under default SIMH.** Run every MMU test with `set mmu cache`. A kernel
> that only worked with the cache off would not have worked on the real machine.

`invd()` is **deleted, not stubbed**: writing РП refills the TLB in the same instruction.

## 12. Switching processes

### `save()` — a BESM-6 `setjmp`

`label_t` is `int[10]`, of which nine are used:

```
      0..6    r1..r7          the callee-saved set
      7       r13             the return address into save()'s caller
      8       r15             the kernel stack pointer
      9       -- unused --
```

`save()` ([`kernel/switch.s`](../kernel/switch.s)) stores those nine and returns 0; when some other
context later calls `resume(paddr, lbl)`, execution reappears there returning 1.

**Not the mode register R** — even though §1 established that the hardware never saves it. That
finding applies to the *gates*, which interrupt arbitrary code mid-function. The ABI fixes R = 7 at
every function entry and exit, so a switch that happens at a call boundary is entitled to the R it was
entered with, in both contexts. There is no ω-mode to carry across.

Mechanically it is a leaf with one parameter, so the argument arrives in the accumulator, nothing is
pushed, and there is no `b$save` prologue.

### `resume()` switches the u-area, not the address space

This is the single biggest divergence from the x86 original, where `resume()` **was** the
address-space switch — and it is what every surviving v7 comment in the tree still gets wrong. The
whole routine is one sentence:

```
      mask; if (paddr != uhome) { if (uhome) uflush(uhome); uload(paddr); uhome = paddr; }
      restore r1-r7, r13, r15 from lbl; A := 1; unmask; jump.
```

Two things changed underneath it:

- **It never writes РП.** The kernel runs unmapped, so reloading the map would change nothing the
  kernel can see. The map is reloaded by `sureg()`, which every landing site that goes on to return to
  user already calls on the `save()`-returned-nonzero arm (`slp.c`, `text.c`). The two landing sites
  without one are correct as they stand: `slp.c`'s second `save()` falls into the process-search loop
  and resumes someone else, and proc 0 has no user map worth loading.
- **The u-area is a fixed *physical* page at `076000`, not a fixed virtual one**, so it has to be
  **copied**: out to the outgoing process's home, in from the incoming one's. That is
  `uflush()`/`uload()` (`kernel/uarea.s`), and it is the price of an unmapped kernel.

> **The label pointer survives the swap by being a constant.** `u.u_qsav` is `076000+n` in *every*
> process, so the pointer may be captured before the copy and dereferenced after it — by which time
> its **contents** are the incoming process's. That is the whole trick.

Four things make it sharper than it reads:

- **`paddr` does not fit an index register.** It is a physical word address over 512 Kwords — 19 bits
  — and index registers are 15. Both arguments are parked in static cells. Non-reentrant, like
  everything on this path.
- **Its own arguments are on the doomed stack.** `paddr` arrives pushed at `r15-1`, inside the page
  `uload()` is about to overwrite, so both arguments must be in their cells *before* the flush.
- **The mask spans both copies, not each one.** `uflush`/`uload` each hold БлПр internally and restore
  ПСВ as they found it; an interrupt landing in the gap between them would build a frame on a kernel
  stack that has just been flushed and is about to be overwritten. So `resume()` holds БлПр itself
  across the pair.
- **It calls `uload()` from assembly**, as that routine's contract requires — `uload` destroys its
  caller's frame, so it keeps all its state in r8/r9/r10 and static cells and never touches r15. Which
  is also why r8/r9/r10 are off limits in `resume()` across both calls.

`uflush`/`uload` themselves steal two virtual pages (1 and 2) to window the two u-areas, both
descriptors living in quartet 0 so one `mod 020` steals them and one puts them back. **Never virtual
page 0** — a store to virtual 0 is dropped and a load returns 0, which would silently zero
`u_rsav[0]` on every switch.

### The u-area invariant

`uhome` names the `p_addr` whose u-area is the one currently live at `076000`; the copy in the
process's own image is **stale between switches**. So anything that reads or frees the current
process's image must flush first. The rule is written up once, at `xswap()` in `text.c`, and
[`kernel/TODO.md`](../kernel/TODO.md) ("The u-area invariant") lists the sites that obey it —
`xswap()` itself (testing `p->p_addr == uhome`, *not* "p is current", because `newproc()` calls
`xswap()` on the child), the `NOUHOME` case, `newproc()`, `expand()` and `exit()`.

> A sixth site, added later and forgotten, will be a very confusing bug.

### `swtch()`

```c
    if (u.u_procp != &proc[0]) {
        if (save(u.u_rsav)) {
            sureg();
            return;
        }
        resume(proc[0].p_addr, u.u_qsav);
    }
    if (save(u.u_qsav) == 0 && save(u.u_rsav))
        return;
loop:
    ...
    if (p == NULL) {
        idle();
        goto loop;
    }
    ...
    resume(p->p_addr, n ? u.u_ssav : u.u_rsav);
```

The `sureg()` on the returned-nonzero arm is what makes `resume()`'s not touching РП correct.

## 13. How it is verified

The kernel now boots on SIMH and **two processes alternate under the real scheduler, each seeing its
own `u`**. Below that, each door has a standalone test that links the *real* kernel objects against a
hand-built environment, forges user mode, and asserts on machine state from the `.ini` script:

| Test | Exercises |
|---|---|
| `crt0t.S` + `utrap.c` | `trapgate` (0500): faults on a closed page, checks the faulting instruction re-executes after the map is opened — from **both** instruction halves |
| `crt0s.S` + `usys.c` | `sysgate` (0577) and `badext` (0550): issues `$77 N` with arguments staged the real way, checks ACC / r14 / r12 / r13 and a balanced r15 |
| `crt0u.S` + `uintr.c` | `intrgate` (0501), including the conditional stack switch |
| `mmutest` | `sureg()` programming РП/РЗ, checked from C *and* by examining РП/РЗ from the `.ini` |
| `uswtch`, `usched`, `uclock` | `save`/`resume`, the scheduler loop, the timer |

`besm6.o` cannot enter a standalone test — its `0500` vector reaches into the C kernel and its
`_start` seeds no stack — which is why `save`/`resume`, `drainbrz`, the u-area bracket and the user
copies live in their own files, and why `syscall.c` is split out of `trap.c`. The tests link the real
thing, not a copy.

**Run every MMU test with `set mmu cache`** (§11).

## 14. Where this differs from Dubna

| Dubna | Ours |
|---|---|
| ИПЗ, the per-task block | the u-area, a **single fixed physical page** at `076000` (§12) |
| `SMASAV`, the global short-save scratch | the trap frame on the kernel stack, plus five temp cells (§4) |
| `FULSAV` / `RETURN` (§6, §7 there) | `trapgate` … `intret` (§6, §10) |
| `SAVIND` (§12 there) | `save()` — [`kernel/switch.s`](../kernel/switch.s) |
| `BOCИПД` + `PUTTMP` | `resume()` — but it switches the **u-area**, not the address space, and never writes РП |
| `PUTTMP` (§10 there) | `sureg()` — [`kernel/utab.c`](../kernel/utab.c) |
| the nine-store drain (§11 there) | `drainbrz()` — [`kernel/brz.s`](../kernel/brz.s) |
| `BADMACRO` | `badext`, the `0550`–`0576` catch-all |
| `SAVS16` / `И16`, the C register (§13 there) | the `CREG` slot in the trap frame |
| `OUTMACRO` (§8 there) | *nothing* — one instruction in the fill replaces it (§10) |

**Taken outright:** the `its`/`sti` store-and-load pipeline (§6, §10), and the internal/external clear
asymmetry (§7, §9).

**Not adopted — the two-tier save.** Dubna saves a minimum in the prologue and materialises the full
context only when the scheduler needs to park a task, which is worth real time on a machine where most
interrupts never park anything. Our gates fill the whole frame unconditionally. The frame is 21 words
rather than Dubna's 24 and the fill is straight-line, so the cost is small — and the invariant *the
frame is always complete* is what lets `trap()`, `syscall()` and `clock()` all read it without asking
which door they came in by. Worth revisiting only if the interrupt path ever shows up in a profile.

**Not needed — `OUTMACRO`.** §10.

**The one difference that matters.** Each Dubna task has its **own** ИПЗ page, separately allocated,
and a context switch just repoints `ГУС` at the incoming task's block — nothing is copied. Ours is
*copied*: the u-area is a single fixed physical page shared across tasks, so `resume()` must
`uflush()` the old and `uload()` the new. That is the price of a one-page u-area, and it is the one we
chose to pay ([`kernel/TODO.md`](../kernel/TODO.md), "Known consequences, accepted"). So Dubna's
`SAVIND` is a closer model for our `save()` than its `BOCИПД` is for our `resume()` — the register
half transfers directly, the u-area half does not.
