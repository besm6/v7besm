# Kernel Assembly Routines (`kernel/x86.s`)

## Purpose of this document

The Unix v7 kernel in this repository is machine-independent C **except for one file**:
[kernel/x86.s](../kernel/x86.s), the *machine-language assist*. Everything that cannot be
expressed in portable C — booting, the trap/interrupt vector, context switching, touching
user memory safely, interrupt masking, port I/O, cache/TLB control — lives here. Today it is
i486/x86 assembly (inherited from Robert Nordier's v7/x86 port, where the file was called
`mch.s`); it is the single file that must be **rewritten from scratch for the BESM-6**.

**That rewrite is done.** The BESM-6 assist now exists and runs on the machine, and it turned out to
be not one file but seven, because [kernel/besm6.S](../kernel/besm6.S) — which holds the vector
block, and so pins symbols at fixed addresses — cannot be linked into a standalone SIMH test.
Anything a test has to exercise for real therefore lives in a file of its own:

| file | holds |
|---|---|
| [besm6.S](../kernel/besm6.S) | `_start`, the vector block at `0500`/`0501` and `0550`–`0577`, and the four gates: `trapgate`, `intrgate`, `sysgate`, `badext` |
| [switch.s](../kernel/switch.s) | `save`, `resume`, and the `uhome` cell |
| [uarea.s](../kernel/uarea.s) | `uflush`, `uload` — the u-area window bracket |
| [seg.s](../kernel/seg.s) | `copyseg`, `clearseg` |
| [usermem.s](../kernel/usermem.s) | `copyin`, `copyout`, `fubyte`, `fuword`, `subyte`, `suword` |
| [psw.s](../kernel/psw.s) | `cli`, `sti` — the read-modify-write of БлПр |
| [brz.s](../kernel/brz.s) | `drainbrz` — the nine-store БРЗ drain |

**It is a much smaller body of assembly than `x86.s`.** The C compiler has the `<besm6.h>` machine
intrinsics ([Intrinsics.md](Intrinsics.md)), so a routine whose whole job is to issue one supervisor
instruction needs no assembly at all: interrupt dispatch is a read of ГРП and a selective clear
([intr.c](../kernel/intr.c)), the page-register load is `sureg()` in
[utab.c](../kernel/utab.c), `idle()` is an ordinary C spin, and every driver's device I/O is a
sequence of `__besm6_ext` control words. What stays in assembly is what no intrinsic can reach: code
that must be entered *with the machine's registers as the hardware left them* (the gates), code that
runs with the kernel's own data unaddressable (the brackets), and code where the **sequence** rather
than the instruction is the contract (`drainbrz`).

Two contracts below did **not** survive the port, and both are noted where they appear: there is no
`nofault` mechanism at all — validation is `useracc()` up front — and `invd()` is deleted rather
than stubbed, because writing РП refills the TLB in the same instruction.

This document specifies, for each routine, its **contract** — arguments, return value, side
effects, and role in the kernel — *independently of the x86 implementation*. The goal is that
the BESM-6 version can be written to satisfy the same contract without reverse-engineering
x86 idioms. It also documents every **global variable and data table** defined in `x86.s`.

Throughout, a distinction is drawn between:

- **Contract-level facts** — visible to the C kernel, part of the interface, must be preserved
  on any target (e.g. `save()` returns 0 the first time and nonzero when resumed).
- **Incidental x86 facts** — implementation detail that disappears or changes form on BESM-6
  (e.g. reloading `%cr3`, the `%eax` return register, the 108-byte FPU frame).

Related references: [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md),
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md),
[Besm6_Data_Representation.md](Besm6_Data_Representation.md), and — for the hardware these routines
must eventually speak to — [Besm6_Peripherals.md](Besm6_Peripherals.md) (the interrupt and I/O side)
and [Memory_Mapping.md](Memory_Mapping.md) (the memory side: what `resume` must do to switch an
address space, what `copyin`/`copyout` cost, and why `invd` has nothing left to do).

### Historical lineage

These routines are the direct descendants of the Unix v7 *machine language assist*
(`sys/conf/mch.s` on the PDP-11): `save`/`resume`/`idle` for context switching (the modernized
form of v6's `savu`/`aretu`/`retu`), `spl0`…`spl7`/`splx` returning the previous priority,
`fubyte`/`suword`/`copyin`/`copyout` for fault-safe user access, and `addupc` for `profil(2)`.
The x86 code here keeps those v7 *contracts* while replacing the PDP-11 mechanisms (processor
priority in the PS word, `mfpd`/`mtpd` previous-space moves, the FP11) with x86 equivalents
(8259 PIC masks, paging, the x87 FPU). When retargeting to BESM-6, the **single most important
invariant to preserve is the `nofault` recovery-PC discipline** (§1) — it is what keeps a stray
user pointer from panicking the kernel — followed by the `save`/`resume` two-return protocol.

---

## 1. Conventions and background

### Calling convention (x86 cdecl — incidental)

On x86 the routines follow cdecl: arguments are on the stack, the first at `4(%esp)` on
entry (the return address occupies `0(%esp)`), the second at `8(%esp)`, and so on; the result
is returned in `%eax`; `%ebx`, `%esi`, `%edi`, `%ebp`, `%esp` are callee-saved. This is why
the routines below start with sequences like `mov 0x4(%esp), %edx`.

On BESM-6 the convention is different (see
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)): arguments are pushed in direct
order, the **last** argument is left in the accumulator, `r14` holds the negative argument
count, and `r13` holds the return address. The *C prototypes* below are the stable interface;
the argument-fetch mechanics are what change.

### The `label_t` type — context save area (contract-level)

```c
typedef int label_t[6];   /* 5 regs and eip  — include/sys/param.h:128 */
```

A `label_t` is the kernel's `jmp_buf`: it holds enough state to resume a kernel thread of
control. On x86 it stores the five callee-saved registers (`%ebx`, `%esp`, `%ebp`, `%esi`,
`%edi`) plus the return address (`EIP`) — six words. It is manipulated only by `save()` and
`resume()`. Three of them live per-process in `struct user`
([include/sys/user.h](../include/sys/user.h)):

| field | line | role |
|-------|------|------|
| `label_t u_rsav` | user.h:21 | save info when exchanging stacks (normal switch) |
| `label_t u_qsav` | user.h:61 | non-local goto target for quits/interrupts during a syscall |
| `label_t u_ssav` | user.h:62 | save info across swapping |

**On BESM-6 `label_t` is `int[10]`** ([param.h](../include/sys/param.h)), of which nine slots are
used — r1–r7 (the callee-saved set), r13 (the return address into `save()`'s caller) and r15 (the
kernel stack pointer). Slot 9 is reserved and unused: shrinking to nine would move `u_upt`, whose
word offset in `struct user` is hardcoded as `UPT = 35` in [uarea.s](../kernel/uarea.s) and
[seg.s](../kernel/seg.s) (`b6as` has no `offsetof`; `mmutest` asserts the value). Ten words cost
three words per process and no risk.

### The user register frame (`u_ar0` / `reg.h`) — contract-level shape

When a trap or syscall occurs, the user's registers are pushed on the kernel stack. `trap()`
sets `u.u_ar0` to point at the saved `EAX`, and the kernel reads/writes user registers as
`u.u_ar0[XX]` using the offsets in [include/sys/reg.h](../include/sys/reg.h):

```c
#define EAX (0)   #define ECX (-1)  #define EDX (-2)  #define EBX (-3)
#define ESP (8)   #define EBP (-9)  #define ESI (-4)  #define EDI (-5)
#define EIP (5)   #define EFL (7)   #define TBIT 0x100  /* single-step flag */
```

`struct trap` (reg.h) mirrors, in order, exactly what the dispatch code (§4) pushes on the
stack, so C `trap(struct trap tr)` can name every saved word (`tr.dev`, `tr.pl`, the GP
registers, `tr.eip`, `tr.efl`, `tr.esp`, …).

**The BESM-6 frame** ([include/sys/reg.h](../include/sys/reg.h)) keeps that pattern and nothing of
the layout. `u.u_ar0` points at word 0 and an index *is* a word index:

```c
#define ACC   0   /* the accumulator          */   #define R15  6
#define RREG  1   /* R, the arithmetic mode   */   #define R14  7
#define RMR   2   /* Y (РМР)                  */   /*  ...           */
#define RET   3   /* the return address       */   #define R1  20
#define SPSW  4   /* СПСВ                     */   #define NREGFRAME 21
#define CREG  5   /* M[16], the C register    */
```

Four differences from the x86 shape are worth naming, because each is a decision rather than a
translation:

- **The register file descends** — `R15` at 6 down to `R1` at 20 — which is the order the Dubna
  `its`/`sti` store-and-load pipeline fills it in ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §6).
- **IRET and ERET collapse into one `RET` slot.** Dubna keeps them separate, but a frame is filled
  by exactly one gate, so only one return address is ever live in it; the gate that built the frame
  picks the matching `выпр` index.
- **ГРП is not framed.** The fault cause is read live with `__besm6_mod(MOD_GRP, 0)`, as Dubna does.
- **There is no flags word.** `EFL`/`TBIT` have no analogue: a syscall error is `errno` in r14
  (`R_ERRNO`, 0 meaning success), a second syscall result is r12 (`R_VAL2`), and single-step is the
  address-break registers М034/М035, not a bit in a saved register.

`USERMODE(spsw)` tests the supervisor bits of the saved mode word; see §3.

### Fault recovery — the `nofault` mechanism, and why BESM-6 has none

`nofault` is a single kernel word ([kernel/x86.s:914](../kernel/x86.s)) holding a *recovery
program counter*. Before a routine touches memory that might fault (user pointers), it stores
the address of a local recovery label into `nofault`; the page-fault path in the trap
dispatcher checks `nofault`, and if nonzero, aborts the faulting instruction and jumps to the
recovery label instead of panicking. It is **not** referenced by any C file — it is entirely
internal to `x86.s`, used by `fubyte`/`fuword`/`subyte`/`suword`/`copyin`/`copyout`.

**The BESM-6 port has no equivalent, deliberately.** The user-access routines validate up front
instead: each calls `useracc()` ([utab.c](../kernel/utab.c)), which walks the shadow map `u.u_upt[]`
and rejects a range that runs into a zero descriptor, and the routine returns a clean C `-1`. There
is no expected-fault path anywhere in the kernel.

That is not merely a different implementation of the same idea — it is what makes the trap gate
simple. Since no supervisor-mode fault is ever *expected*, one taken in supervisor mode is by
definition a kernel bug, so `trapgate` may switch to the kernel stack unconditionally and `trap()`
may panic on it. See §3, and the corresponding note in
[Memory_Mapping.md](Memory_Mapping.md#what-this-means-for-the-v7-kernel), which recommends the
catch-`GRP_OPRND_PROT` approach that this kernel did not take.

### Interrupt priority model (`spl`, `pl`, `iq`) — contract-level

Unix uses *processor priority levels*: raising the level blocks interrupts of that class and
below, so a critical section cannot be preempted by a device it shares data with. The classic
v7 scheme is `spl0` (allow everything) up through `spl7` (block everything); each `splN()`
**returns the previous level** so it can be restored with `splx(old)`. This port keeps the
level in the private cell `pl` and, because the 8259 PIC cannot express arbitrary levels,
implements the mask with a *deferred/soft interrupt queue* `iq`: an interrupt that arrives
while masked is recorded in `iq` and replayed by `unqint` when the level drops. The level
constants are 16-bit interrupt masks:

```
IPL0 = 0xffff   allow all          IPL5 = 0xbd01
IPL1 = 0xfdfb                       IPL6 = 0xbd00
IPL4 = 0xfd41                       IPL7 = 0        block all
```

The macro `BASEPRI(pl)` (`param.h:145`, `(pl) != 0xffff`) tells the clock whether it
interrupted a critical section.

**On BESM-6 there are two levels, not eight**, and the v7 spelling survives over them: callers still
write `s = spl5(); … ; splx(s);` and still get what they were after, since on a uniprocessor with no
atomic instruction, masking interrupts is the only lock there is. Only `spl0` enables; every `splN`
above it blocks. Delivery needs БлПр clear **and** `ГРП & МГРП` non-zero, so either register could
have been the mask, and the kernel divides them:

- **БлПр (ПСВ bit `02000`) is the priority**, set and cleared through `cli()`/`sti()`
  ([psw.s](../kernel/psw.s)). The hardware already treats it as one — a gate forces БлПр on at the
  vector and `выпр` restores it from СПСВ, so returning through a gate re-establishes the level by
  itself, exactly as the PDP-11's `rtt` does. МГРП, outside the mode word, does nothing of the kind.
- **МГРП is the source enable**, armed once by `intrinit()` from `main()` and never rewritten.

The reverse assignment was tried first and is a trap: with the level in МГРП, `spl0()` opens the
source mask while the gates still hold БлПр, so **no interrupt can be taken in kernel mode at all**.
That is invisible while every interrupt arrives in user mode, and becomes fatal the moment `idle()`
must spin waiting for one. The full argument is at the head of [intr.c](../kernel/intr.c).

There is no soft interrupt queue: `iq`, `unqint` and the deferred-replay machinery are gone, because
two levels need no arbitration. **`BASEPRI(x)` is permanently `(0)`** — `setipl()` leaves interrupts
deliverable only at spl0, so anything `clock()` interrupts was at base priority by construction.
(Note the v7 name reads backwards: *true* means "was **above** base, skip the callouts".)

`HZ` is **250**, not v7's 60: the interval timer free-runs at that rate (SIMH `CLK_TPS`, per the
original documentation) and **cannot be programmed**, so `clkstart()` has nothing to set up — it
dismisses the tick accumulated during boot and calls `spl0()`. v7 userland that hardcodes 60
(`/bin/time`, `ps`) will misreport until it is fixed.

---

## 2. Boot / initialization path (`_start`)

`_start` ([x86.s:34](../kernel/x86.s)) is the kernel entry point. It is **replaced wholesale
on BESM-6** (different CPU bring-up, no x86 real mode, no 8259/8253, and an MMU that works nothing
like x86 paging — [Memory_Mapping.md](Memory_Mapping.md)), so it is documented only at a level
sufficient to reproduce its *outputs*. In order it:

1. **Enters 32-bit protected mode** — `cli`, load the GDT (`gdtd`), set `PE` in `%cr0`, far
   jump to flush the pipeline; reload the segment registers and a temporary stack.
2. **Clears BSS** — zero from `edata` to `end` (rounded up a page) and record the first free
   address in **`kend`**; then zero nine more pages used for the initial page tables, u-area,
   and kernel stack.
3. **Enables the A20 gate** and **sizes physical memory** by writing/reading test patterns
   from 1 MB upward (trusting the first megabyte, capping at ~14 MB reserved). The page count
   is stored in **`phymem`**. (`startup()` in `machdep.c` later frees this into `coremap`.)
4. **Builds the initial page tables** with the fixed layout described in the source comment:

   ```
   page 0    page directory
   page 1    user page table
   pages 2-5 core (physical-memory window) page tables
   page 6    kernel page table
   page 7    u. area
   page 8    kernel stack
   ```

   This establishes the fixed virtual addresses exported as `u`, `mem`, `pdir`, `upt`, `kpt`,
   `kstk` (§6).
5. **Enables paging** (`PG` in `%cr0`), reloads the GDT at its high virtual alias, switches to
   the real kernel stack `kstk`.
6. **Builds the IDT** from the compact control string `idtctl` (§4), programs the two **8259
   PICs** (ICW1–ICW4, remapping IRQ0–15 to vectors 0x20–0x2f), and sets up the **TSS** used for
   the ring-0 stack on privilege transitions.
7. **`call main`** — enters the C kernel. When `main` returns (having built process 1's image),
   `_start` loads the task register and does an `iret` that drops into **user mode** at the
   `icode` bootstrap.

For the port, the essential deliverables of this phase are: a zeroed BSS, a known `phymem`, a valid
initial address space with the u-area and kernel stack mapped, a live trap vector, and a first
transition into user mode running `icode` ([machdep.c:27](../kernel/machdep.c)). Note `kend` is *not*
among them: `b6ld` defines the boundary symbol `end` (first word past the whole image) as soon as
something references it — see [Linker_Manual.md](Linker_Manual.md) §4.3 — so the BESM-6 boot code
zeroes BSS from `edata` to `end` and keeps no such variable of its own.

### BESM-6 notes — done, and almost nothing survives

**`_start` is two instructions**: seed r15 from `machdep.c`'s `int *const ustkbase = &u.u_stack[0]`
(≈ `076214`) and call `main()`. Steps 1, 4, 5 and 6 above have no counterpart at all — the machine
**resets straight into the kernel's own mode**, supervisor with mapping, protection and interrupts
all off ([Memory_Mapping.md](Memory_Mapping.md#reset-state)), and the vector block is not *built* but
*laid out by the linker* at fixed addresses in the const segment, which is why
[besm6.o must come first](../kernel/Makefile) in the link order.

Steps 2 and 3 moved **into C, at the top of `main()`**, and neither for stylistic reasons:

- Bss-zero is `wzero(edata, end - edata)`. It cannot be in `_start` because the size is a difference
  of two linker externals and `b6as` rejects that expression. It is guarded out under `#ifdef
  ON_SIMH`, since SIMH starts every word at zero.
- **Memory is not sized, it is asserted**: `phymem = 512 * 1024`. A kernel running unmapped can
  reach 32 Kwords, so it has no way to probe the 512 Kword store — there is nowhere to write the
  test pattern. The number is the machine's, not a measurement.

`uhome` is initialised in `main()` immediately after `proc[0].p_addr`; see
[TODO.md](../kernel/TODO.md), "The u-area invariant". `make run` with
[kernel/unix.ini](../kernel/unix.ini) boots the image under SIMH.

---

## 3. Trap and interrupt dispatch

All exceptions, hardware IRQs, and the system-call gate funnel through hand-written stubs that
save state, call a C handler, and restore state. This is the second big block that is
**rewritten for BESM-6** (its own trap/extracode mechanism), but the *shape* — build a
`struct trap` frame, dispatch by cause, honor `nofault`, replay deferred interrupts, check
`runrun` — is the model to reproduce.

### Vector stubs

`idtctl` ([x86.s:837](../kernel/x86.s)) is a compact description consumed at boot to fill the
IDT with gate descriptors pointing at these tables:

- **`intx00`…`intx10`** — CPU exceptions #DE, #DB, NMI, #BP, #OF, #BR, #UD, #NM, #DF, #TS, #NP,
  #SS, #GP, #PF, #MF. Each pushes its trap number and jumps to `trap0` (errorless faults, which
  synthesize a dummy error word) or `trap` (faults that push a hardware error code).
- **`intx20`…** — the 16 hardware IRQs; each pushes vector `0x20+n` and jumps to `call`.
- **`intx30`** — the system-call gate (`int $0x30`); pushes `0x30` and falls into `trap0`.

### `trap` / `trap0` — exception and syscall path

`trap0` normalizes an errorless fault to have a zero error word; `trap` then checks **`nofault`**
first: if a recovery PC is set (a fault expected inside `fubyte`/`copyin`/etc.), it discards the
frame and `iret`s straight to that PC — the fast recovery path. Otherwise it builds the full
`struct trap` frame (segment regs, GP regs, trap number, error, `eip`/`cs`/`eflags`) and calls
the C function `trap()` ([kernel/trap.c:33](../kernel/trap.c)), which decodes the cause, sends
signals, or (for vector 0x30) reads syscall arguments with `fuword`, dispatches through
`sysent[]`, and posts the result/errno back into the user register frame.

### `call` / `call1` — hardware interrupt path

`call` saves the caller-clobbered registers, switches to kernel data segments, and computes the
IRQ index. It **acknowledges the PIC** (EOI) and consults `pl`: if the interrupt is currently
masked, it merely records the IRQ in **`iq`** (the soft-interrupt queue) and returns — deferring
it. Otherwise it raises `pl` to that IRQ's level from **`_ipltbl`**, re-enables interrupts, and
calls the handler from **`_inttbl`** (e.g. `clock`, `scrint`, `srintr`, `hdintr`). After the
handler, if it interrupted user mode and `runrun` is set, it forces a reschedule; finally it
calls `unqint` to replay any interrupts that queued in `iq` while the level was high, restores
registers, and `iret`s.

### `unqint` — deferred interrupt replay

`unqint` ([x86.s:491](../kernel/x86.s)) pops the highest-priority pending IRQ out of `iq` that is
now unmasked by `pl`, and calls its stub, looping until none remain. This is how the
software-level scheme delivers interrupts that hardware masking deferred; it is invoked both at
the end of interrupt handling and by `splx`/`spl*` when the level drops.

### BESM-6 notes — four gates, two save disciplines, one exit

Done, in [besm6.S](../kernel/besm6.S). There is no IDT to build and no PIC to acknowledge: the
hardware vectors to **fixed const-segment words** — `0500` internal fault, `0501` external
interrupt, `0550`–`0577` the extracodes, one word each. `unqint` and `iq` have no counterpart (§1:
two levels need no deferred queue). The four gates are:

| gate | vector | door |
|---|---|---|
| `trapgate` | `0500` | internal fault → `trap()` |
| `intrgate` | `0501` | external interrupt → `extintr()` |
| `sysgate` | `0577` (э77) | the system call → `syscall()` |
| `badext` | `0550`–`0576` | every other extracode → `badextr()`, which posts SIGINS |

**Two save disciplines.** A fault or an interrupt lands between arbitrary instructions, so the
interrupted code owns *every* register and the gate must save the full visible machine — including R
and Y, which the hardware does **not** save ([Unix_Context_Switch.md](Unix_Context_Switch.md) §1). An
extracode is a synchronous *call*: the caller owns its live registers, so the gate saves almost
nothing, and the hardware has already clobbered r14 with the effective address.

**One exit.** All four leave through `intret`, `intrgate`'s restore block. An extracode's return
address is in ERET and a fault's in IRET, and Dubna solves this by normalising one into the other
(`OUTMACRO`, [Dubna_Context_Switch.md](Dubna_Context_Switch.md) §8). **That turned out to be unnecessary here:**
the frame is filled by a `its`/`sti` pipeline that reads a return register *live*, so `sysgate` is
`trapgate` with exactly one instruction changed — `its ERET` where the fault gate has `its IRET` —
and `intret` is reused unmodified. Its closing `выпр` index selects only *which register holds the
PC*; the mode comes from СПСВ either way.

**The stack switch is the sharp edge.** r15 is **not banked by mode**: there is one stack register
shared across modes, so a gate entered from user must repoint it at the kernel stack by hand, and a
gate that nests inside the kernel must leave it alone. The signal is **`СПСВ & 014`**
(РежЭ | РежПр), zero *iff* the interrupted context was user. Test that, **not** БлП: `copyin`
runs in supervisor mode with БлП clear, so a БлП test would misread a fault taken mid-`copyin` as
"from user" and reset r15 out from under the syscall frame.

- `trapgate` and `sysgate` switch **unconditionally**, so their frame is always at the link-time
  constant `[ustkbase]` and `trap()` opens with `(struct trap *)u.u_stack` rather than taking an
  argument. This is legitimate only because there is no `nofault` path (§1): a supervisor fault is
  a kernel bug, and resetting r15 under a panic costs nothing.
- `intrgate` switches **conditionally** — it genuinely nests — so its frame base is a run-time
  value, and it publishes it in a private `intrframe` cell for `clock()`. Using `u.u_ar0` for this
  was tried and is wrong: a tick nested inside a syscall would overwrite the interrupted syscall's
  `u_ar0`, and `exec()` and `sendsig()` write through that pointer from paths that sleep.

**`badext` carries its own third copy of the prologue** rather than sharing `sysgate`'s body behind
a discriminator. Nothing before the frame is filled can tell the doors apart — the hardware
identifies an extracode purely by which vector word it landed on — so sharing would cost a flag and
a branch to save a block that is otherwise identical.

**The restart fixup lives in C**, at the top of `trap()`, because the frame is aliased in place: the
saved PC is the faulting word plus one and `SPSW_RIGHT_INSTR` already names the half, so the whole
correction is `tr->ret--` and clearing `SPSW_NEXT_RK`. The derivation and the verified recipe are in
[Memory_Mapping.md](Memory_Mapping.md#the-restart-protocol--read-this-before-writing-a-fault-handler).

---

## 4. Exported routines

Each routine is given with its C prototype (from
[include/sys/systm.h](../include/sys/systm.h)), purpose, return value, side effects, principal
callers, and BESM-6 porting notes.

### 4.1 Interrupt priority — `spl0`/`spl1`/`spl4`/`spl5`/`spl6`/`spl7`, `splx`

```c
int  spl0(void), spl1(void), spl4(void), spl5(void), spl6(void), spl7(void);  /* systm.h:149 */
void splx(int s);                                                             /* systm.h:150 */
```

- **Purpose.** Set the processor interrupt priority level. `splN` sets a fixed level; `splx(s)`
  restores a previously saved one.
- **Return.** Each `splN` returns the **previous** value of `pl` (an `int` level cookie); the
  idiom is `s = spl6(); … ; splx(s);`.
- **Side effects.** Updates `pl`; on lowering the level, calls `unqint` to replay any interrupts
  deferred in `iq`.
- **Levels.** `spl0` = allow all; `spl6` = block scheduler/buffer-critical device interrupts;
  `spl7` = block everything; `spl5` = block disk/floppy-class devices; `spl1`, `spl4` are
  intermediate. (`spl4` is declared but currently has no C caller.)
- **Callers.** Pervasive: `slp.c` (`sleep`/`wakeup`/`setrq`/`sched`/`swtch` use `spl6`/`spl0`),
  `clock.c:62,105,153`, `prim.c`, and the device drivers (`dev/bio.c`, `dev/tty.c`, `dev/hd.c`,
  `dev/fd.c`, `dev/cd.c`).
- **BESM-6 notes — done**, in [intr.c](../kernel/intr.c) over `cli`/`sti`
  ([psw.s](../kernel/psw.s)). The 16-bit masks (`IPL*`) and the `iq`/`unqint` deferral are
  x86/8259 artifacts and are gone: there are **two levels**, the knob is **БлПр** and not МГРП, and
  nothing needs deferring. The v7 spelling and the return-the-old-level contract are preserved
  intact. See §1, "Interrupt priority model", for why the register choice is not free.

  One consequence that only appeared on the machine: `splx()` inside an interrupt handler must
  repair the **software shadow only** (`curipl = s`), not the hardware bit. Clearing БлПр with the
  interrupted state still in СПСВ/IRET lets the free-running timer re-enter the handler
  immediately and forever. Restoring the bit is `выпр`'s job, not `splx()`'s.

### 4.2 Context switch — `save`, `resume`

```c
int  save(label_t);          /* systm.h:196 */
void resume(short paddr, label_t);  /* systm.h:197 */
```

These are the heart of process switching — the v7 `savu`/`aretu`/`resume` primitives.

- **`save(label)`** is `setjmp`-style. It stores the callee-saved registers and the return
  address into `label` and returns **0** on the direct call. Later, when some other thread of
  control does `resume(…, label)`, execution reappears *as if `save` returned again*, this time
  returning **nonzero** (1). Callers branch on the result:

  ```c
  if (save(u.u_rsav)) {   /* nonzero: we were just resumed */
      sureg();
      return;
  }
  /* zero: first time through — go pick another process */
  ```

- **`resume(paddr, label)`** — *on the BESM-6 this switches the u-area, NOT the address space; see
  the note at the end of this section.* On x86 it switches to the address space of the process whose swappable image
  is at physical click `paddr` (on x86: rewrite the user/kernel page-table entries `kptu`/`kptk`
  and reload `%cr3`), then reloads the registers and stack from `label` and jumps — i.e. it
  performs the `longjmp` counterpart of the `save` that filled `label`. It does not return to its
  caller in the normal sense; control resurfaces inside the matching `save`.
  On BESM-6 the switch is twelve `рег` writes (eight of РП, four of РЗ) — but the **БРЗ write cache
  must be drained first**, or dirty lines from the outgoing process are written back through the
  incoming process's mapping. See
  [Memory_Mapping.md](Memory_Mapping.md#the-брз-write-cache-and-the-брс-prefetch-buffer).
- **Return.** `save` → 0 (direct) / nonzero (via resume). `resume` → does not return.
- **Callers.** `swtch()` ([slp.c:352](../kernel/slp.c)) — the scheduler core; `sleep()`
  ([slp.c:88](../kernel/slp.c)) resumes at `u_qsav` after a signal; `newproc()`
  ([slp.c:508](../kernel/slp.c)) and `expand()` ([slp.c:564](../kernel/slp.c)) use `u_ssav`;
  `xalloc` in `text.c:162`; syscall entry `trap.c:135` arms `u_qsav` so an interrupted syscall
  can longjmp out.
- **BESM-6 notes — DONE (task 16), in [kernel/switch.s](../kernel/switch.s).** The return-value
  protocol is preserved exactly; the address-space half of the x86 semantics is **not**, because
  it does not exist here.

  `save()` stores nine slots — r1–r7, r13, r15 — and nothing else. In particular *not* the mode
  register R: the ABI fixes R = 7 at every function entry and exit, so the R that `resume()` is
  entered with is by construction the R the `save()` it reappears in is entitled to return with.
  (§14's "the hardware never saves R" is true, and applies to the *gates*, which interrupt
  arbitrary code mid-function — not to a switch that happens at a call boundary in both
  contexts.) `label_t` is `int[10]`, of which nine are used and slot 9 is reserved; it was left
  at ten deliberately, because shrinking it moves `u_upt` and `uarea.s` hardcodes that offset.

  **`resume()` switches the u-area, and never writes РП.** Two things that held on the x86 hold
  here no longer: the kernel runs unmapped, so reloading РП would change nothing it can see (the
  map is reloaded by `sureg()`, which every landing site that returns to user already calls on
  the `save()`-returned-nonzero arm); and the u-area is a fixed *physical* page, so it has to be
  **copied** — `uflush()` out to the outgoing home, `uload()` in from the incoming one. The label
  pointer survives that copy by being the constant `076000 + n` in every process, which is why it
  may be captured before the swap and dereferenced after it. The kernel-side rules for who must
  flush and when are written up once, at `xswap()` in [text.c](../kernel/text.c).

### 4.3 Idle — `idle`

```c
void idle(void);   /* systm.h:184 */
```

- **Purpose.** Called by `swtch()` when no process is runnable: lower the level to allow all
  interrupts, enable interrupts, and halt (`hlt`) until one arrives; then restore the level and
  return. The scheduler loops back and re-scans the run queue.
- **Side effects.** Temporarily sets `pl = IPL0`; the halt instruction's address is published in
  the global **`waitloc`** so that if the clock interrupt lands on the idle `hlt`, `clock()`
  ([clock.c:92](../kernel/clock.c)) attributes the tick to idle rather than to the kernel.
- **Callers.** `swtch()` ([slp.c:408](../kernel/slp.c)); also `prf.c:95` (panic spin).
- **BESM-6 notes — DONE (task 16), and it did not stay in assembly.** There is no
  wait-for-interrupt on this machine: the only halt is `033` (`стоп`), which is resumable on real
  hardware only from the operator's console and which SIMH, `dubna` and `b6sim` alike treat as
  "the run is over". So `idle()` is a **spin**, and since `spl0()` now clears БлПр itself (the
  interrupt priority moved onto that bit — see `spl0`…`spl7` below) there were no mode bits left
  for assembly to poke. It is ordinary C, in [intr.c](../kernel/intr.c).
  **`waitloc` is deleted, not ported.** With no halt instruction to point at, the pc comparison
  would have had to be calibrated against what the hardware saves, and would drift whenever the
  code around it was recompiled. The idle spin raises a flag, `idling`, instead; `extintr()`
  clears it after servicing anything at all, which both releases the spin — that is what makes
  the spin behave like `hlt` — and leaves `clock()` an exact test for the accounting.

### 4.4 User-space access with fault protection

```c
int fubyte(caddr_t addr);            /* systm.h:167  fetch user byte  */
int fuword(caddr_t addr);            /* systm.h:168  fetch user word  */
int subyte(caddr_t addr, int value); /* systm.h:165  store user byte  */
int suword(caddr_t addr, int value); /* systm.h:166  store user word  */
int copyin (caddr_t from, caddr_t to, unsigned nbytes);  /* systm.h:178 */
int copyout(caddr_t from, caddr_t to, unsigned nbytes);  /* systm.h:177 */
```

- **Purpose.** Move data across the user/kernel boundary **safely**: a bad user address must not
  panic the kernel. `fubyte`/`fuword` fetch one byte/word from user space; `subyte`/`suword`
  store one; `copyin`/`copyout` move a block from/to user space.
- **Return.** `fubyte`/`fuword` return the value fetched, or **−1** on fault. `subyte`/`suword`
  return **0** on success, **−1** on fault. `copyin`/`copyout` return **0**/**−1**.
- **Mechanism.** Each first validates the address range (`ckr` for reads, `ckw` for writes —
  ensuring the pointer lies within the user's mapped region and, for writes, in a writable page),
  then arms **`nofault`** with a recovery label, performs the access, and disarms `nofault`. A
  fault during the access lands on the recovery label, which returns −1. The internal helpers
  `bwfsu`/`bwssu` (byte/word fetch/store setup) and `cpisu`/`cposu` (copy-in/out setup) factor the
  `ckr`/`ckw` + `nofault` boilerplate.
- **Callers.** `fuword` reads syscall arguments in `trap.c:130`; `suword`/`subyte` write results
  and signal frames (`sendsig` in `machdep.c:106`, `sig.c`, `sys1.c`); `copyin`/`copyout` are the
  core of `iomove` ([rdwri.c:181](../kernel/rdwri.c)), `exec`/`icode` setup (`main.c:88`,
  `sys3.c`, `sys4.c`), and tty I/O (`dev/tty.c`).
- **BESM-6 notes — done**, in [usermem.s](../kernel/usermem.s). The return contracts are preserved
  exactly; the mechanism is not.

  **There is no window and no map switch.** A trap does not disturb РП, so the user's map is
  *already loaded*: the loop toggles **БлП per word** — read the user word mapped, store it to the
  kernel buffer unmapped — and runs entirely out of index registers, because while mapping is on
  the kernel's own data is not addressable. No `drainbrz` either: РП is never written, and a mapped
  store goes back through the same loaded map, so there is no tag hazard.

  **There is no `nofault` path at all** (§1). Validation is `useracc()`, called from the routine
  itself, and a range running into a zero descriptor returns the clean C `-1` — the compiler's own
  `-1`, not 48 ones, so `fubyte(…) == -1` matches at a C caller.

  **`copyin`/`copyout` are word-only**, exactly as the x86 originals are: they copy `nbytes / NBPW`
  whole words, every caller passes a word-aligned address and a word-multiple count, and an
  unaligned copy stays on the `fubyte`/`subyte` byte path inside `iomove`.

  The byte variants do emulate sub-word access by read-modify-write, as predicted — but note the
  **fat-pointer marker bit (48) is load-bearing**. `fubyte` extracts its byte with `asx` on the
  pointer's own exponent field, which is `64 + 8·off`; a `char *` built by hand from an `int` has no
  marker, so the shift becomes `8·off − 64` and the fetch returns 0. Only the compiler's
  `int*`→`char*` conversion produces a usable pointer. See
  [Besm6_Data_Representation.md](Besm6_Data_Representation.md).

### 4.4a The mapped brackets — `copyseg`, `clearseg`, `uflush`, `uload`

```c
void copyseg(unsigned from, unsigned to);   /* one page, word addresses */
void clearseg(unsigned addr);
void uflush(unsigned paddr);                /* live u-area -> the process's home */
void uload(unsigned paddr);                 /* the process's home -> live u-area  */
```

These have no x86 counterpart worth speaking of (`copyseg`/`clearseg` were `bcopy`/`bzero` through
the `PHY` window), but they are the characteristic BESM-6 routine, so they are documented here.

The problem they solve: an unmapped kernel reaches only the low 32 Kwords, so **any physical page
above `0100000` — the page pool, and therefore every process image — is unaddressable**. Each of
these routines steals two virtual pages as windows with one `mod 020`, copies register-only, and
puts the quartet back from `u.u_upt[]`.

Three constraints, all of them non-obvious and all of them verified on the machine by
[mmutest](../kernel/test/mmutest.c) under `set mmu cache`:

- **The windows are virtual pages 1 and 2 — never page 0.** A store to virtual address 0 is dropped
  and a load returns 0; the test is on the *virtual* address, before translation, so the black hole
  follows the window wherever page 0 is mapped. A window there silently loses word 0 of whatever it
  copies. Pages 1 and 2 are also the cheap pair: they share quartet 0, so one `mod 020` steals both,
  and their addresses fit the 12-bit short address field, so the copy loop needs no `utc`.
- **The БРЗ is drained on both sides of the copy**, and the two drains cover different hazards — the
  leading one is the standing "drain before every РП write" rule, the trailing one is what makes the
  copy reach memory before the map changes back. `mmutest` fails distinctly on each.
- **The bracket holds БлПр**, which means saying `vtm 02002`/`02003` rather than a bare `vtm 2`:
  `vtm N,0` writes БлПр along with БлП and БлЗ, so the plain form *enables* interrupts as a side
  effect. For `uload` that is fatal — it is overwriting the very page an interrupt would build its
  frame in.

That last point is also `uload`'s calling contract: **it destroys its caller's kernel stack frame**,
so only `resume()` — assembly, keeping its state in registers — may call it. `uflush()` only reads
the live page and is safe from C.

### 4.5 Floating-point state — `savfp`, `restfp`, `stst`

```c
void savfp(void *ptr);   /* systm.h:189  save FPU state   */
void restfp(void *ptr);  /* systm.h:190  restore FPU state */
void stst(int *ptr);     /* systm.h:215  store FPU status  */
```

- **Purpose.** `savfp` saves the full FPU register/state image (x86: 108-byte `fnsave` frame) to
  `ptr`; `restfp` restores it (`frstor`); `stst` stores the FPU control/status word
  (`fnstcw`) — the diagnostic "why did the FPU fault" query. It is the descendant of the v7
  PDP-11 `STST` (store status) instruction, which captured the floating exception code and
  address so the trap handler could post the correct signal.
- **Side effects / usage.** Uses the lazy-save pattern keyed on `u.u_fpsaved`: `swtch()` saves FP
  state only if not already saved ([slp.c:365](../kernel/slp.c)); `trap()` restores it on the way
  out if it was saved ([trap.c:167](../kernel/trap.c)); an FP-error trap calls
  `stst(&u.u_fper)` to capture the error code ([trap.c:115,121](../kernel/trap.c)). The targets
  are `struct user` fields `u_fps` (108-byte state), `u_fper` (error word), and the `u_fpsaved`
  flag ([user.h:22-26](../include/sys/user.h)).
- **BESM-6 notes.** The BESM-6 has its own (non-IEEE-754) float format and no separate FPU state
  frame; these routines shrink to whatever floating context (if any) must be preserved across a
  switch. The 108-byte size is purely x86.

### 4.6 Profiling — `addupc`

```c
void addupc(int pc, void *prof, int incr);   /* systm.h:151 */
```

- **Purpose.** Support for `profil(2)`: given the interrupted user PC and the process's profiling
  descriptor, increment the histogram bucket that `pc` maps into. `prof` points at
  `struct { short *pr_base; unsigned pr_size; unsigned pr_off; unsigned pr_scale; }`
  ([user.h:69-74](../include/sys/user.h)); `addupc` computes `bucket = ((pc - pr_off) * pr_scale)
  >> N`, and if in range, adds `incr` to `pr_base[bucket]`.
- **Side effects.** Writes into the user's profiling buffer through the `nofault` guard; a fault
  disarms profiling by zeroing `pr_scale`.
- **Callers.** `clock.c:87` (one tick, when `pr_scale` set) and `trap.c:165` (syscall CPU time).
  Armed by the `profil` syscall (`sys4.c:340`), cleared on `exec` (`sys1.c:218`).
- **BESM-6 notes — still a stub**, so `profil(2)` is inert. Straightforward to port when it is
  wanted; only the fixed-point scale shift and the buffer addressing need adjusting for word
  addressing, and the `nofault` guard becomes a `useracc()` check (§4.4). Idle-time accounting,
  which `addupc`'s neighbourhood in `clock()` used to depend on, went the other way — see §4.3.

### 4.7 Memory primitives — `bcopy`, `bzero`

```c
void bcopy(const void *src, void *dst, unsigned len);  /* systm.h:143 */
void bzero(void *dst, unsigned len);                   /* systm.h:144 */
```

- **Purpose.** Kernel-to-kernel block copy and zero-fill (no fault protection — both operands are
  kernel addresses).
- **Callers.** `bcopy`: `alloc.c`, `iget.c:305`, `main.c:118`, `nami.c:156`, `sys1.c:282`,
  `sys3.c:180`, `utab.c:87` (`copyseg`), `dev/md.c`, `dev/fd.c`. `bzero`: `utab.c:76` (`clearseg`),
  `dev/bio.c:379`, `dev/cd.c:217`.
- **BESM-6 notes — done, and renamed.** They are **`wcopy`/`wzero`, and they take a WORD count**,
  not a byte count: every call site converts with `btow()` ([param.h](../include/sys/param.h)), and
  the whole-block copies use `BSIZEW` (512 words). Putting the conversion at the call sites rather
  than inside the routine is what keeps the loop pure — no six-chars-per-word tail to handle. The
  body is `copyin`'s inner loop minus the per-word БлП toggle, the validation and the ПСВ save:
  plain unmapped, register-only, no window and no drain. `aax #077777` strips a caller's fat
  pointer to a 15-bit word address.

  A knock-on worth knowing: `DIRSIZ` is now 24, so `struct direct` is 5 words and directory entries
  are word-aligned — which is what lets `nami.c` use the word copy at all.

### 4.8 Port I/O — `inb`, `outb`, `insw`, `outsw`

```c
int  inb(int addr);                     /* systm.h:159 */
void outb(int addr, int value);         /* systm.h:158 */
void insw (int addr, char *buf, int n); /* systm.h:161 */
void outsw(int addr, char *buf, int n); /* systm.h:160 */
```

- **Purpose.** x86 programmed I/O: read/write a byte on an I/O port; read/write `n` 16-bit words
  between a port and memory (block PIO).
- **Callers.** `machdep.c` (PIT/RTC/floppy-motor), and the disk/tty/beeper drivers
  (`dev/hd.c`, `dev/cd.c`, `dev/fd.c`, `dev/sc.c`); e.g. `(hd.rd ? insw : outsw)(DATA, …)` in
  `dev/hd.c:278`.
- **BESM-6 notes — deleted, all seven.** BESM-6 has no port space, and no assembly stands in for
  them: a driver reaches a device with the `__besm6_ext` intrinsic (`033 «увв»`) directly from C —
  see [Intrinsics.md](Intrinsics.md) and [Besm6_Peripherals.md](Besm6_Peripherals.md). `dev/hd.c`
  became `dev/md.c` (disks) and `dev/mb.c` (drums), which with `dev/sr.c` survive as **driver
  skeletons** with their port I/O stripped to `// TODO`, still
  wired into `conf.c` so their `bdevsw`/`cdevsw` hooks resolve; `machdep.c` lost the 8253 PIT and
  the CMOS RTC with them, which is why nothing seeds `time` from a wall clock.

### 4.9 Control registers, cache/TLB, interrupt flag — `ld_cr0`/`ld_cr2`/`ld_cr3`, `invd`, `cli`/`sti`

```c
int  ld_cr0(void), ld_cr2(void), ld_cr3(void);  /* systm.h:214 */
void invd(void);                                /* systm.h:217 */
void cli(void);                                 /* systm.h:162 */
void sti(void);                                 /* systm.h:163 */
```

- **`ld_cr0/2/3`** read the x86 control registers (cr0 = machine status, cr2 = faulting linear
  address, cr3 = page-directory base). Only caller: the panic/trap register dump in `trap.c:55`.
  Pure x86 — no BESM-6 analogue beyond "read whatever status registers exist".
- **`invd`** flushes the TLB by reloading `%cr3`. Caller: `utab.c:51` after `sureg()` rewrites the
  user page table `upt`. On BESM-6 it is **deleted, not stubbed**: writing a page register refills
  the corresponding TLB entries in the same instruction, so a stale translation is not a state the
  machine can be in ([Memory_Mapping.md](Memory_Mapping.md#the-registers)). A no-op would have
  invited someone to wonder when it needs calling.
- **`cli`/`sti`** clear/set the CPU interrupt-enable flag around very short critical sequences
  (programming the PIT in `machdep.c:89-93`, the beeper timer in `dev/sc.c:560-564`).

  **On BESM-6 they carry far more weight than that**: they are the interrupt priority level itself
  (§1, §4.1), and every `spl*` is built on them. They live in [psw.s](../kernel/psw.s), and the
  implementation is a **read-modify-write** of the БлПр bit of ПСВ — `ita 021`, then `aox 02000` or
  `aax 075777`, then `ati 021`. It is emphatically *not* a `vtm`, which writes the whole mode word
  and would clobber БлП/БлЗ/ПОП/ПОК along with the bit being changed. (Supervisor mode takes a
  5-bit register number, which is what makes `M[021]` reachable at all.)

---

## 5. Global variables and data tables defined in `x86.s`

### Contract-level globals (referenced by C)

| symbol | x86.s | C declaration | meaning |
|--------|-------|---------------|---------|
| `u` | 922 (`.set u, U`) | `extern struct user u;` (user.h:101) | the per-process user area; holds the kernel stack and per-process state. On x86 it is *mapped* at a fixed virtual address (page 7). **On BESM-6 it is a fixed PHYSICAL page** — `u = 076000`, an absolute symbol rather than storage — and therefore has to be **copied** in and out on a context switch (§4.2, §4.4a) |
| `kend` | 916 | *(dropped in the port)* | first free physical address after the kernel; used to size the initial process and free core. The BESM-6 kernel has no such variable: `b6ld`'s boundary symbol `end` already names the first word past the image, and nothing in C needs it — `startup()` frees core from `0100000` and `main()` sets `proc[0].p_addr` outright |
| `phymem` | 919 | `extern int phymem;` (machdep.c:18) | physical memory size, found by the boot memory scan; `startup()` frees it into `coremap`. On BESM-6 it is a **count of words**, and it is asserted rather than probed — `phymem = 512 * 1024` in `main()`, because an unmapped kernel cannot reach the store it would have to write test patterns into (§2) |
| `waitloc` | 893 | *(deleted in the port)* | PC of the idle `hlt`; the clock compared the interrupted PC against it to charge idle time. This machine has no halt to point at, so task 16 replaced it with the `idling` flag (`intr.c`), which is exact and cannot drift when the code is recompiled |
| `mem` | 926 (`.set mem, 0x40000000`) | (used via macros) | base of the window that maps *all* physical memory into the kernel address space (`PHY` in `utab.c`) |
| `pdir` | 927 (`.set pdir, 0x7ff9a000`) | `extern int pdir[];` (utab.c:14) | virtual address of the page directory; read by `physaddr()` |
| `upt` | 928 (`.set upt, 0x7ff9b000`) | `extern int upt[];` (utab.c:14) | virtual address of the **user page table**; rewritten by `sureg()` to map the current process's text/data/stack |

These last three had **no BESM-6 counterpart at all, and are deleted.** The machine has no page table
in memory, no page-directory base register and no page walk — the entire mapping is eight write-only
registers — and no 32-bit window can hold 512 Kwords of physical memory. They were briefly kept as
placeholder arrays so `utab.c` would link; removing them, together with the x86 FP state and the bss
`u`, gave back **2563 words** of bss. What replaces them is the per-process **shadow** page table
`u.u_upt[8]`, blasted out to РП/РЗ by `sureg()` in twelve `рег`s. See
[Memory_Mapping.md](Memory_Mapping.md#what-this-means-for-the-v7-kernel).

`pl` is also contract-*adjacent*: it is a private cell ([x86.s:890](../kernel/x86.s)), but its
value at trap time is captured into `struct trap.pl` (reg.h:24) and tested by `BASEPRI()`
(param.h:145) in `clock.c`. On BESM-6 the frame carries no priority slot and `BASEPRI(x)` is
permanently `(0)` — see §1.

### Internal globals and data tables (private to `x86.s`)

| symbol | x86.s | meaning |
|--------|-------|---------|
| `pl` | 890 | current processor priority level (16-bit interrupt mask); manipulated by `spl*`/`splx`/`call` |
| `iq` | 891 | soft interrupt queue: bitmask of IRQs that arrived while masked, replayed by `unqint` |
| `nofault` | 914 | fault-recovery PC; nonzero means "an access may fault — jump here instead of panicking" (see §1) |
| `_inttbl` | 845 | IRQ → handler dispatch table (`clock`, `scrint`, `stray`, `srintr`, `fdintr`, `hdintr`, `cdintr`, …) |
| `_ipltbl` | 863 | IRQ → priority level to raise while the handler runs |
| `idtctl` | 837 | compact control string the boot code expands into IDT gate descriptors |
| `gdt`/`gdtd`/`gdtd2` | 882/903/907 | the global descriptor table and its load-descriptors (kernel/user code+data segments) |
| `ivt`/`idtd` | 896/911 | the interrupt descriptor table storage and its load-descriptor |
| `tss` | 899 | task-state segment, supplies the ring-0 stack on privilege transitions |
| `wait_loc` | 688 | the idle `hlt` PC that `waitloc` points at |
| `kpt`/`kptu`/`kptk`/`kstk` | 930-933 | fixed virtual addresses of the kernel page table, the two page-table entries `resume` rewrites to remap the current process, and the kernel stack top |

The x86-descriptor machinery (`gdt`, `ivt`, `tss`, `idtctl`, the IPL masks, `iq`) has no direct
BESM-6 equivalent and is replaced by the BESM-6 trap/interrupt setup. The tables `_inttbl` and
`_ipltbl` conceptually survive as "interrupt source → handler / level" maps, though their entries
change with the device set.

---

## 6. Porting summary for BESM-6

| routine(s) | effort on BESM-6 |
|------------|------------------|
| `bcopy`, `bzero` | **done** — renamed `wcopy`/`wzero` and they take a **word** count, converted by `btow()` at every call site, so the loop has no six-chars-per-word tail |
| `spl0`…`spl7`, `splx` | **done** — two levels, not eight, and the knob is **БлПр** (via `cli`/`sti`), not МГРП, which is a source enable armed once by `intrinit()`. Putting the level in the mode word is what lets `выпр` restore it on a gate return, as the PDP-11's `rtt` does |
| `addupc` | **still a stub**, so `profil(2)` is inert — same histogram logic when wanted, adjusting the fixed-point scale and word addressing |
| `cli`, `sti` | **done** — [psw.s](../kernel/psw.s); a read-modify-write of БлПр in ПСВ, never a `vtm`, and they now carry the whole interrupt priority level |
| `fubyte`/`fuword`/`subyte`/`suword`, `copyin`/`copyout` | **done** — [usermem.s](../kernel/usermem.s); **no `nofault` recovery was needed**, validation is `useracc()` up front. No window either: the loop toggles БлП per word through the user map that is already loaded. Byte variants do RMW, and mind the fat-pointer marker bit |
| `copyseg`/`clearseg`, `uflush`/`uload` | **done** — [seg.s](../kernel/seg.s), [uarea.s](../kernel/uarea.s); the characteristic BESM-6 shape, a two-page window bracket with a БРЗ drain either side (§4.4a). No x86 counterpart |
| `save`, `resume` | **done (task 16)** — [kernel/switch.s](../kernel/switch.s); nine slots (r1–r7, r13, r15), and `resume()` switches the **u-area**, not the address space: it never writes РП (see below) |
| `idle` | **done (task 16)** — no wait-for-interrupt exists, so it is a spin released by `extintr()`; written in C, and `waitloc` is deleted in favour of the `idling` flag |
| `savfp`/`restfp`/`stst` | **deleted** — the x86 FP state went with the stage-1 bss cleanup; the mode register R is saved by the gates instead (§3), and there is no separate float context to preserve |
| `inb`/`outb`/`insw`/`outsw` | **deleted**, all seven — a driver issues `033 «увв»` from C via `__besm6_ext`; no assembly stands in for them |
| `ld_cr0/2/3`, `invd` | **deleted** — `invd` is not even a no-op: writing РП refills the TLB in the same instruction, so there is nothing to invalidate and no call site to justify |
| `_start`, trap/IRQ/syscall dispatch (§2–3) | **done** — `_start` is two instructions (the machine resets into the kernel's own mode) with bss-zero and `phymem` moved into C; dispatch is four gates, two save disciplines and one shared exit (§3). `nofault` has no counterpart; `runrun` survives |

Remember the calling-convention shift when re-coding any of these: BESM-6 passes arguments in
direct order with the last argument in the accumulator, `r14` = negative arg count, `r13` =
return address (see [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)), and every C
scalar occupies one 48-bit word (see [Besm6_Data_Representation.md](Besm6_Data_Representation.md)).
