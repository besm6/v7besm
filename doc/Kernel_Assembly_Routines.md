# Kernel Assembly Routines

## Purpose of this document

The Unix v7 kernel in this repository is machine-independent C except for its
*machine-language assist*: everything that cannot be expressed in portable C — booting, the
trap/interrupt vector, context switching, touching user memory safely, interrupt masking.

The assist is not one file but seven, because [kernel/besm6.S](../kernel/besm6.S) — which holds
the vector block, and so pins symbols at fixed addresses — cannot be linked into a standalone
SIMH test. Anything a test has to exercise for real therefore lives in a file of its own:

| file | holds |
|---|---|
| [besm6.S](../kernel/besm6.S) | `_start`, the vector block at `0500`/`0501` and `0550`–`0577`, and the four gates: `trapgate`, `intrgate`, `sysgate`, `badext` |
| [switch.s](../kernel/switch.s) | `save`, `resume`, and the `uhome` cell |
| [uarea.S](../kernel/uarea.S) | `uflush`, `uload` — the u-area window bracket |
| [seg.S](../kernel/seg.S) | `copyseg`, `clearseg` |
| [usermem.S](../kernel/usermem.S) | `copyin`, `copyout`, `fubyte`, `fuword`, `subyte`, `suword` |
| [psw.s](../kernel/psw.s) | `cli`, `sti` — the read-modify-write of БлПр — and `getpsw`, which reads ПСВ back |
| [brz.s](../kernel/brz.s) | `drainbrz` — the nine-store БРЗ drain |

**It is a small body of assembly.** The C compiler has the `<besm6.h>` machine
intrinsics ([Intrinsics.md](Intrinsics.md)), so a routine whose whole job is to issue one supervisor
instruction needs no assembly at all: interrupt dispatch is a read of ГРП and a selective clear
([intr.c](../kernel/intr.c)), the page-register load is `sureg()` in
[utab.c](../kernel/utab.c), `idle()` is an ordinary C spin, and every driver's device I/O is a
sequence of `__besm6_ext` control words. What stays in assembly is what no intrinsic can reach: code
that must be entered *with the machine's registers as the hardware left them* (the gates), code that
runs with the kernel's own data unaddressable (the brackets), and code where the **sequence** rather
than the instruction is the contract (`drainbrz`).

Two contracts v7 relied on have **no counterpart here**, and both are noted where they appear:
there is no `nofault` mechanism at all — validation is `useracc()` up front — and `invd()` does
not exist, because writing РП refills the TLB in the same instruction.

This document specifies, for each routine, its **contract** — arguments, return value, side
effects, and role in the kernel — so that a C caller can be read against it without opening the
assembly. It also documents every **global variable and data table** the assist defines.
Facts stated as contract-level are visible to the C kernel and part of the interface
(e.g. `save()` returns 0 the first time and nonzero when resumed).

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
Those v7 *contracts* are what this assist keeps; the PDP-11 mechanisms behind them (processor
priority in the PS word, `mfpd`/`mtpd` previous-space moves, the FP11) all have BESM-6
replacements, described routine by routine below. The one to hold on to hardest is the
`save`/`resume` two-return protocol.

---

## 1. Conventions and background

### Calling convention

See [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md): arguments are pushed in direct
order, the **last** argument is left in the accumulator, `r14` holds the negative argument
count, and `r13` holds the return address. The *C prototypes* below are the stable interface.

### The `label_t` type — context save area (contract-level)

```c
typedef int label_t[10];   /* program status: r1-r7, r13, r15 — include/sys/types.h:20 */
```

A `label_t` is the kernel's `jmp_buf`: it holds enough state to resume a kernel thread of
control. It is manipulated only by `save()` and `resume()`. Three of them live per-process in
`struct user` ([include/sys/user.h](../include/sys/user.h)):

| field | line | role |
|-------|------|------|
| `label_t u_rsav` | user.h:20 | save info when exchanging stacks (normal switch) |
| `label_t u_qsav` | user.h:60 | non-local goto target for quits/interrupts during a syscall |
| `label_t u_ssav` | user.h:61 | save info across swapping |

Nine of the ten slots are used — r1–r7 (the callee-saved set), r13 (the return address into
`save()`'s caller) and r15 (the kernel stack pointer). Slot 9 is reserved and unused:
shrinking to nine would move `u_upt`, whose
word offset in `struct user` is hardcoded as `UPT = 35` in [uarea.S](../kernel/uarea.S) and
[seg.S](../kernel/seg.S) (`b6as` has no `offsetof`; `mmutest` asserts the value). Ten words cost
three words per process and no risk.

### The user register frame (`u_ar0` / `reg.h`) — contract-level shape

When a trap or syscall occurs, the user's registers are saved on the kernel stack. `trap()`
sets `u.u_ar0` to point at the base of that frame, and the kernel reads/writes user registers
as `u.u_ar0[XX]` using the offsets in [include/sys/reg.h](../include/sys/reg.h). `struct trap`
(reg.h) mirrors, in order, exactly what the dispatch code (§4) saves, so C
`trap(struct trap tr)` can name every saved word.

`u.u_ar0` points at word 0 and an index *is* a word index:

```c
#define ACC   0   /* the accumulator          */   #define R15  6
#define RREG  1   /* R, the arithmetic mode   */   #define R14  7
#define RMR   2   /* Y (РМР)                  */   /*  ...           */
#define RET   3   /* the return address       */   #define R1  20
#define SPSW  4   /* СПСВ                     */   #define NREGFRAME 21
#define CREG  5   /* M[16], the C register    */
```

Four properties of this frame are worth naming, because each is a decision rather than a
translation:

- **The register file descends** — `R15` at 6 down to `R1` at 20 — which is the order the Dubna
  `its`/`sti` store-and-load pipeline fills it in ([Dubna_Context_Switch.md](Dubna_Context_Switch.md) §6).
- **IRET and ERET collapse into one `RET` slot.** Dubna keeps them separate, but a frame is filled
  by exactly one gate, so only one return address is ever live in it; the gate that built the frame
  picks the matching `выпр` index.
- **ГРП is not framed.** The fault cause is read live with `__besm6_mod(MOD_GRP, 0)`, as Dubna does.
- **There is no flags word.** A syscall error is `errno` in r14
  (`R_ERRNO`, 0 meaning success), a second syscall result is r12 (`R_VAL2`), and single-step is the
  address-break registers М034/М035, not a bit in a saved register.

`USERMODE(spsw)` tests the supervisor bits of the saved mode word; see §3.

### Fault recovery — why there is no `nofault` mechanism

Earlier Unix ports carried a `nofault` cell: a *recovery program counter* that a routine set
before touching memory that might fault, so the page-fault path could abort the faulting
instruction and jump to a recovery label instead of panicking.

**This kernel has no equivalent, deliberately.** The user-access routines validate up front
instead: each calls `useracc()` ([utab.c](../kernel/utab.c)), which walks the shadow map `u.u_upt[]`
and rejects a range that runs into a zero descriptor, and the routine returns a clean C `-1`. There
is no expected-fault path anywhere in the kernel.

That is not merely a different implementation of the same idea — it is what makes the trap gate
simple. Since no supervisor-mode fault is ever *expected*, one taken in supervisor mode is by
definition a kernel bug, so `trapgate` may switch to the kernel stack unconditionally and `trap()`
may panic on it. See §3, and the corresponding note in
[Memory_Mapping.md](Memory_Mapping.md#what-this-means-for-the-v7-kernel), which recommends the
catch-`GRP_OPRND_PROT` approach that this kernel did not take.

### Interrupt priority model (`spl`) — contract-level

Unix uses *processor priority levels*: raising the level blocks interrupts of that class and
below, so a critical section cannot be preempted by a device it shares data with. The classic
v7 scheme is `spl0` (allow everything) up through `spl7` (block everything); each `splN()`
**returns the previous level** so it can be restored with `splx(old)`.

**Here there are two levels, not eight**, and the v7 spelling survives over them: callers still
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

There is no soft interrupt queue and no deferred-replay machinery, because two levels need no
arbitration. **`BASEPRI(x)` is permanently `(0)`** — `setipl()` leaves interrupts
deliverable only at spl0, so anything `clock()` interrupts was at base priority by construction.
(Note the v7 name reads backwards: *true* means "was **above** base, skip the callouts".)

`HZ` is **250**, not v7's 60: the interval timer free-runs at that rate (SIMH `CLK_TPS`, per the
original documentation) and **cannot be programmed**, so `clkstart()` has nothing to set up — it
dismisses the tick accumulated during boot and calls `spl0()`. v7 userland that hardcodes 60
(`/bin/time`, `ps`) will misreport until it is fixed.

---

## 2. Boot / initialization path (`_start`)

`_start` ([besm6.S](../kernel/besm6.S)) is the kernel entry point, and it is **two
instructions**: seed r15 from `machdep.c`'s `int *const ustkbase = &u.u_stack[0]` (≈ `076214`)
and call `main()`.

There is almost nothing for it to do. The machine **resets straight into the kernel's own
mode** — supervisor, with mapping, protection and interrupts all off
([Memory_Mapping.md](Memory_Mapping.md#reset-state)) — so there is no mode to enter and no MMU
to bring up. The vector block is not *built* at boot but *laid out by the linker* at fixed
addresses in the const segment, which is why
[besm6.o must come first](../kernel/Makefile) in the link order.

What a boot path must still deliver is: a zeroed BSS, a known `phymem`, a live trap vector, and
a first transition into user mode running `icode` ([machdep.c:26](../kernel/machdep.c)). The
first two happen **in C, at the top of `main()`**, and neither for stylistic reasons:

- Bss-zero is `wzero(edata, end - edata)` — `b6ld` defines the boundary symbol `end`, the first
  word past the whole image, as soon as something references it (see
  [Linker_Manual.md](Linker_Manual.md) §4.3), so the kernel needs no `kend` variable of its own.
  It cannot be in `_start` because the size is a difference of two linker externals and `b6as`
  rejects that expression. It is guarded out under `#ifdef ON_SIMH`, since SIMH starts every
  word at zero.
- **Memory is not sized, it is asserted**: `phymem = 512 * 1024`. A kernel running unmapped can
  reach 32 Kwords, so it has no way to probe the 512 Kword store — there is nowhere to write the
  test pattern. The number is the machine's, not a measurement.

`uhome` is initialised in `main()` immediately after `proc[0].p_addr`; see
[TODO.md](../kernel/TODO.md), "The u-area invariant". `make run` with
[kernel/unix.ini](../kernel/unix.ini) boots the image under SIMH.

---

## 3. Trap and interrupt dispatch

All faults, external interrupts and the system-call door funnel through hand-written gates that
save state, call a C handler, and restore state. The shape they implement is v7's: build a
`struct trap` frame, dispatch by cause, check `runrun` on the way out.

### Four gates, two save disciplines, one exit

The gates live in [besm6.S](../kernel/besm6.S). There is no descriptor table to build at boot
and no interrupt controller to acknowledge: the hardware vectors to **fixed const-segment
words** — `0500` internal fault, `0501` external interrupt, `0550`–`0577` the extracodes, one
word each. Nor is there a deferred-interrupt queue to replay (§1: two levels need no
arbitration). The four gates are:

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

**`intret` shuts the interrupt door as its own first act**, and that is what makes the shared exit
safe. The three synchronous gates clear БлПр before calling C — v7's `spl0()`-on-entry, without
which a system call would run to completion with the clock stopped — so the epilogue cannot assume
it was entered blocked. It must be: below its first three instructions it reloads СПСВ and IRET,
single registers the hardware overwrites the instant an interrupt is taken, and re-stashes into the
five shared temp cells. Enforcing the level there rather than in each C exit path is the difference
between one place to get right and one per path forever.

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
- **Return.** Each `splN` returns the **previous** level (an `int` cookie); the
  idiom is `s = spl6(); … ; splx(s);`.
- **Levels.** `spl0` = allow all; `spl6` = block scheduler/buffer-critical device interrupts;
  `spl7` = block everything; `spl5` = block disk/floppy-class devices; `spl1`, `spl4` are
  intermediate. (`spl4` is declared but currently has no C caller.)
- **Callers.** Pervasive: `slp.c` (`sleep`/`wakeup`/`setrq`/`sched`/`swtch` use `spl6`/`spl0`),
  `clock.c:62,105,153`, `prim.c`, and the device drivers (`dev/bio.c`, `dev/tty.c`, `dev/hd.c`,
  `dev/fd.c`, `dev/cd.c`).
- **Implementation — done**, in [intr.c](../kernel/intr.c) over `cli`/`sti`
  ([psw.s](../kernel/psw.s)). There are **two levels**, the knob is **БлПр** and not МГРП, and
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

- **`resume(paddr, label)`** is the `longjmp` counterpart of the `save` that filled `label`: it
  brings in the u-area of the process whose swappable image is at physical address `paddr`, then
  reloads the registers and stack from `label` and jumps. It does not return to its caller in the
  normal sense; control resurfaces inside the matching `save`. **It switches the u-area, not the
  address space** — see the note at the end of this section. Where a map *is* loaded (`sureg()`,
  twelve `рег` writes: eight of РП, four of РЗ), the **БРЗ write cache must be drained first**, or
  dirty lines from the outgoing process are written back through the incoming process's mapping.
  See [Memory_Mapping.md](Memory_Mapping.md#the-брз-write-cache-and-the-брс-prefetch-buffer).
- **Return.** `save` → 0 (direct) / nonzero (via resume). `resume` → does not return.
- **Callers.** `swtch()` ([slp.c:352](../kernel/slp.c)) — the scheduler core; `sleep()`
  ([slp.c:88](../kernel/slp.c)) resumes at `u_qsav` after a signal; `newproc()`
  ([slp.c:508](../kernel/slp.c)) and `expand()` ([slp.c:564](../kernel/slp.c)) use `u_ssav`;
  `xalloc` in `text.c:162`; syscall entry `trap.c:134` arms `u_qsav` so an interrupted syscall
  can longjmp out.
- **Implementation — DONE (task 16), in [kernel/switch.s](../kernel/switch.s).** The v7
  return-value protocol is preserved exactly.

  `save()` stores nine slots — r1–r7, r13, r15 — and nothing else. In particular *not* the mode
  register R: the ABI fixes R = 7 at every function entry and exit, so the R that `resume()` is
  entered with is by construction the R the `save()` it reappears in is entitled to return with.
  (§14's "the hardware never saves R" is true, and applies to the *gates*, which interrupt
  arbitrary code mid-function — not to a switch that happens at a call boundary in both
  contexts.) `label_t` is `int[10]`, of which nine are used and slot 9 is reserved; it was left
  at ten deliberately, because shrinking it moves `u_upt` and `uarea.S` hardcodes that offset.

  **`resume()` switches the u-area, and never writes РП.** Two properties of this machine put it
  that way: the kernel runs unmapped, so reloading РП would change nothing it can see (the
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
  interrupts and wait until one arrives; then restore the level and return. The scheduler loops
  back and re-scans the run queue.
- **Side effects.** Temporarily drops to `spl0`, and raises the global flag **`idling`** so that
  `clock()` ([clock.c:90](../kernel/clock.c)) attributes a tick landing here to idle rather than
  to the kernel.
- **Callers.** `swtch()` ([slp.c:408](../kernel/slp.c)); also `prf.c:94` (panic spin).
- **Implementation — DONE (task 16), and it is not assembly.** There is no
  wait-for-interrupt on this machine: the only halt is `033` (`стоп`), which is resumable on real
  hardware only from the operator's console and which SIMH, `dubna` and `b6sim` alike treat as
  "the run is over". So `idle()` is a **spin**, and since `spl0()` clears БлПр itself (the
  interrupt priority lives on that bit — see `spl0`…`spl7` below) there are no mode bits left
  for assembly to poke. It is ordinary C, in [intr.c](../kernel/intr.c).

  The `idling` flag is what gives the spin an exit: `extintr()` clears it after servicing
  anything at all, so the spin ends on the first interrupt. Idle-time accounting rides on the
  same flag rather than on a pc comparison, which would have to be calibrated against what the
  hardware saves and would drift whenever the code around it was recompiled.

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
- **Mechanism.** Each validates the address range up front with `useracc()` — ensuring the pointer
  lies within the user's mapped region and, for writes, in a writable page — and returns −1 if it
  does not. The access itself is then known to be safe; there is no recovery path behind it
  (§1).
- **Callers.** `fuword` reads syscall arguments in `trap.c:129`; `suword`/`subyte` write results
  and signal frames (`sendsig` in `machdep.c:106`, `sig.c`, `sys1.c`); `copyin`/`copyout` are the
  core of `iomove` ([rdwri.c:180](../kernel/rdwri.c)), `exec`/`icode` setup (`main.c:88`,
  `sys3.c`, `sys4.c`), and tty I/O (`dev/tty.c`).
- **BESM-6 notes — done**, in [usermem.S](../kernel/usermem.S). The return contracts are preserved
  exactly; the mechanism is not.

  **There is no window and no map switch.** A trap does not disturb РП, so the user's map is
  *already loaded*: the loop toggles **БлП per word** — read the user word mapped, store it to the
  kernel buffer unmapped — and runs entirely out of index registers, because while mapping is on
  the kernel's own data is not addressable. No `drainbrz` either: РП is never written, and a mapped
  store goes back through the same loaded map, so there is no tag hazard.

  **There is no `nofault` path at all** (§1). Validation is `useracc()`, called from the routine
  itself, and a range running into a zero descriptor returns the clean C `-1` — the compiler's own
  `-1`, not 48 ones, so `fubyte(…) == -1` matches at a C caller.

  **`copyin`/`copyout` are word-only**: they copy `nbytes / NBPW`
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

These are the characteristic BESM-6 routine, and the shape recurs wherever the kernel must
reach memory it cannot normally address.

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

### 4.5 Profiling — `addupc`

```c
void addupc(int pc, void *prof, int incr);   /* systm.h:151 */
```

- **Purpose.** Support for `profil(2)`: given the interrupted user PC and the process's profiling
  descriptor, increment the histogram bucket that `pc` maps into. `prof` points at
  `struct { short *pr_base; unsigned pr_size; unsigned pr_off; unsigned pr_scale; }`
  ([user.h:68-73](../include/sys/user.h)); `addupc` computes `bucket = ((pc - pr_off) * pr_scale)
  >> N`, and if in range, adds `incr` to `pr_base[bucket]`.
- **Side effects.** Writes into the user's profiling buffer; an inaccessible buffer disarms
  profiling by zeroing `pr_scale`.
- **Callers.** `clock.c:87` (one tick, when `pr_scale` set) and `trap.c:164` (syscall CPU time).
  Armed by the `profil` syscall (`sys4.c:340`), cleared on `exec` (`sys1.c:216`).
- **Status — still a stub**, so `profil(2)` is inert. Straightforward to write when it is
  wanted; only the fixed-point scale shift and the buffer addressing need care for word
  addressing, and the range check is a `useracc()` call (§4.4). Idle-time accounting, which
  `addupc`'s neighbourhood in `clock()` sits next to, works differently — see §4.3.

### 4.6 Memory primitives — `bcopy`, `bzero`

```c
void bcopy(const void *src, void *dst, unsigned len);  /* systm.h:143 */
void bzero(void *dst, unsigned len);                   /* systm.h:144 */
```

- **Purpose.** Kernel-to-kernel block copy and zero-fill (no fault protection — both operands are
  kernel addresses).
- **Callers.** `bcopy`: `alloc.c`, `iget.c:304`, `main.c:118`, `nami.c:156`, `sys1.c:282`,
  `sys3.c:180`, `utab.c:84` (`copyseg`), `dev/md.c`, `dev/fd.c`. `bzero`: `utab.c:73` (`clearseg`),
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

### 4.7 The interrupt flag — `cli`, `sti`

```c
void cli(void);                                 /* systm.h:162 */
void sti(void);                                 /* systm.h:163 */
int  getpsw(void);                              /* not declared in systm.h -- see below */
```

- **Purpose.** Clear and set the interrupt-enable bit. **They are the interrupt priority level
  itself** (§1, §4.1), and every `spl*` is built on them. They live in [psw.s](../kernel/psw.s),
  and the
  implementation is a **read-modify-write** of the БлПр bit of ПСВ — `ita 021`, then `aox 02000` or
  `aax 075777`, then `ati 021`. It is emphatically *not* a `vtm`, which writes the whole mode word
  and would clobber БлП/БлЗ/ПОП/ПОК along with the bit being changed. (Supervisor mode takes a
  5-bit register number, which is what makes `M[021]` reachable at all.)

  The gates do **not** call these two, though they perform exactly the same read-modify-write: the
  three synchronous ones inline `sti` before their C call and `intret` inlines `cli` at its top
  ([Unix_Context_Switch.md](Unix_Context_Switch.md) §10). `13 vjm sti` would be unreadable inside a
  block where `sti N` is also a *machine instruction* — `intret` uses it a dozen times to restore
  registers — and the call would clobber r13 for no gain over three inline words.

- **`getpsw` reads ПСВ back**, which is the only way to see the interrupt level from C: unlike РП
  and РЗ, the mode word is readable. The kernel never calls it — it tracks the level in `curipl` and
  reads the *interrupted* mode word out of the trap frame — so `libunix.a`'s link-pull drops it from
  the image. It exists for `kernel/test/usys.c`, which asserts from inside a `sysent` stub that the
  extracode gate really did open БлПр before dispatching (`F_IPL`). Nothing else can check that: the
  level is a hardware bit, and every C-visible shadow of it would agree either way.

### 4.8 What the assist does not contain

Three jobs that a machine-language assist often carries have no routine here at all, and each
absence is a property of the hardware rather than an omission:

- **No port I/O.** There is no I/O address space. A driver reaches a device with the
  `__besm6_ext` intrinsic (`033 «увв»`) directly from C — see [Intrinsics.md](Intrinsics.md)
  and [Besm6_Peripherals.md](Besm6_Peripherals.md) — so no assembly stands between the two.
- **No TLB flush.** Writing a page register refills the corresponding TLB entries in the same
  instruction, so a stale translation is not a state the machine can be in
  ([Memory_Mapping.md](Memory_Mapping.md#the-registers)). There is deliberately not even a
  no-op, which would invite someone to wonder when it needs calling.
- **No floating-point context.** The BESM-6 float format is the machine's own, carried in the
  ordinary accumulator and mode register; there is no separate FPU state frame to save, and the
  mode register R is saved by the gates (§3) along with everything else.

---

## 5. Globals defined by the assist

### Contract-level globals (referenced by C)

| symbol | C declaration | meaning |
|--------|---------------|---------|
| `u` | `extern struct user u;` (user.h) | the per-process user area; holds the kernel stack and per-process state. It is a fixed **physical** page — `u = 076000`, an absolute symbol rather than storage — and therefore has to be **copied** in and out on a context switch (§4.2, §4.4a) |
| `phymem` | `extern int phymem;` (machdep.c) | physical memory size in **words**; `startup()` frees it into `coremap`. It is asserted rather than probed — `phymem = 512 * 1024` in `main()`, because an unmapped kernel cannot reach the store it would have to write test patterns into (§2) |

There is deliberately **no page table in memory** and so no `pdir`/`upt`/`mem` globals naming
one: the machine has no page-directory base register and no page walk — the entire mapping is
eight write-only registers — and no window large enough to hold 512 Kwords of physical memory.
What serves instead is the per-process **shadow** page table `u.u_upt[8]`, blasted out to РП/РЗ
by `sureg()` in twelve `рег`s. See
[Memory_Mapping.md](Memory_Mapping.md#what-this-means-for-the-v7-kernel).

There is likewise no `kend`: `b6ld`'s boundary symbol `end` already names the first word past
the image, and nothing in C needs more — `startup()` frees core from `0100000` and `main()`
sets `proc[0].p_addr` outright.

`BASEPRI(x)`, which v7 used to ask whether the clock interrupted a critical section, is
permanently `(0)` here: the trap frame carries no priority slot, because there are only two
levels and the level lives in the saved mode word (§1).

### Internal globals

| symbol | meaning |
|--------|---------|
| `uhome` | physical address whose u-area is currently live in the fixed page; the cell `resume()` consults to decide whether to flush and reload. Defined in [switch.s](../kernel/switch.s), initialised in `main()`; see [TODO.md](../kernel/TODO.md), "The u-area invariant" |

Two C globals are contract-*adjacent* — declared in `systm.h`, but read or written by the
assembly: **`idling`** (raised by the idle spin, cleared by `extintr()`; §4.3) and **`runrun`**,
the reschedule-pending flag the gates test on the way back to user mode (§3).

---

## 6. Status of the assist

| routine(s) | state |
|------------|-------|
| `bcopy`, `bzero` | **done** — renamed `wcopy`/`wzero` and they take a **word** count, converted by `btow()` at every call site, so the loop has no six-chars-per-word tail |
| `spl0`…`spl7`, `splx` | **done** — two levels, not eight, and the knob is **БлПр** (via `cli`/`sti`), not МГРП, which is a source enable armed once by `intrinit()`. Putting the level in the mode word is what lets `выпр` restore it on a gate return, as the PDP-11's `rtt` does |
| `cli`, `sti`, `getpsw` | **done** — [psw.s](../kernel/psw.s); a read-modify-write of БлПр in ПСВ, never a `vtm`, and they carry the whole interrupt priority level. The gates inline the same sequence rather than call it. `getpsw` reads ПСВ back, for the test that checks a gate opened the level |
| `fubyte`/`fuword`/`subyte`/`suword`, `copyin`/`copyout` | **done** — [usermem.S](../kernel/usermem.S); **no fault-recovery path**, validation is `useracc()` up front. No window either: the loop toggles БлП per word through the user map that is already loaded. Byte variants do RMW, and mind the fat-pointer marker bit |
| `copyseg`/`clearseg`, `uflush`/`uload` | **done** — [seg.S](../kernel/seg.S), [uarea.S](../kernel/uarea.S); a two-page window bracket with a БРЗ drain either side (§4.4a) |
| `save`, `resume` | **done (task 16)** — [kernel/switch.s](../kernel/switch.s); nine slots (r1–r7, r13, r15), and `resume()` switches the **u-area**, not the address space: it never writes РП |
| `idle` | **done (task 16)** — no wait-for-interrupt exists, so it is a spin released by `extintr()`, written in C over the `idling` flag |
| `_start`, trap/interrupt/extracode dispatch (§2–3) | **done** — `_start` is two instructions (the machine resets into the kernel's own mode) with bss-zero and `phymem` in C; dispatch is four gates, two save disciplines and one shared exit (§3) |
| `addupc` | **still a stub**, so `profil(2)` is inert — same histogram logic when wanted, adjusting the fixed-point scale and word addressing |
| port I/O, TLB flush, FP context | **not present, by design** — see §4.8 |

Remember the calling convention when working on any of these: arguments in
direct order with the last argument in the accumulator, `r14` = negative arg count, `r13` =
return address (see [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)), and every C
scalar occupies one 48-bit word (see [Besm6_Data_Representation.md](Besm6_Data_Representation.md)).
