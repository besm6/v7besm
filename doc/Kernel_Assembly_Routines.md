# Kernel Assembly Routines (`kernel/x86.s`)

## Purpose of this document

The Unix v7 kernel in this repository is machine-independent C **except for one file**:
[kernel/x86.s](../kernel/x86.s), the *machine-language assist*. Everything that cannot be
expressed in portable C — booting, the trap/interrupt vector, context switching, touching
user memory safely, interrupt masking, port I/O, cache/TLB control — lives here. Today it is
i486/x86 assembly (inherited from Robert Nordier's v7/x86 port, where the file was called
`mch.s`); it is the single file that must be **rewritten from scratch for the BESM-6**.

That rewrite has a home already: [kernel/besm6.S](../kernel/besm6.S) is the BESM-6 counterpart,
currently a **skeleton** — the symbols exist so the kernel builds and links with the BESM-6
toolchain, but the bodies are still to be written, against the contracts specified below.

**It will be a much smaller file than `x86.s`.** The C compiler now has the `<besm6.h>` machine
intrinsics ([Intrinsics.md](Intrinsics.md)), so a routine whose whole job is to issue one
supervisor instruction no longer needs assembly at all: `spl0`…`spl7`/`splx` are a write of the
МГРП mask (`__besm6_mod(036, …)`), interrupt dispatch is a read of ГРП and a selective clear, the
address-space switch inside `resume` is a write of the page registers, and every driver's device
I/O is a sequence of `__besm6_ext` control words — all of it C. What genuinely stays in
[kernel/besm6.S](../kernel/besm6.S) is what no intrinsic can reach: the boot entry, the interrupt
and extracode vector (`besm6.S` must be entered *with the machine's registers as the hardware left
them*), the register-save/restore of `save`/`resume`, and the `nofault` fault-recovery mechanism.
Each contract below still holds; the language it is implemented in is now a choice.

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

On BESM-6 `label_t` must be resized to hold the BESM-6 callee-saved registers plus the return
address; the *count* (6) is an x86 detail, the *purpose* is not.

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
registers, `tr.eip`, `tr.efl`, `tr.esp`, …). On BESM-6 the *set* of saved registers changes
but the pattern — a per-trap frame the C code indexes symbolically — is preserved.

### The `nofault` mechanism — contract-level idea, x86 implementation

`nofault` is a single kernel word ([kernel/x86.s:914](../kernel/x86.s)) holding a *recovery
program counter*. Before a routine touches memory that might fault (user pointers), it stores
the address of a local recovery label into `nofault`; the page-fault path in the trap
dispatcher checks `nofault`, and if nonzero, aborts the faulting instruction and jumps to the
recovery label instead of panicking. It is **not** referenced by any C file — it is entirely
internal to `x86.s`, used by `fubyte`/`fuword`/`subyte`/`suword`/`copyin`/`copyout`. The
BESM-6 port needs an equivalent "expected fault → recover" hook in its own trap handler.

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
interrupted a critical section. On BESM-6 the numeric masks are x86-specific, but the
*contract* — six/seven named entry points that set a level and return the old one — carries
over.

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

For the port, the essential deliverables of this phase are: a zeroed BSS, a known `kend` and
`phymem`, a valid initial address space with the u-area and kernel stack mapped, a live trap
vector, and a first transition into user mode running `icode` ([machdep.c:27](../kernel/machdep.c)).

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
- **BESM-6 notes.** The 16-bit masks (`IPL*`) and the `iq`/`unqint` deferral are x86/8259
  artifacts. Reimplement as: keep a level in `pl`, map each level to the target's interrupt
  mask, return the old level, and provide an equivalent deferred-delivery path if the hardware
  cannot mask by class.

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

- **`resume(paddr, label)`** switches to the address space of the process whose swappable image
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
- **BESM-6 notes.** The register set saved and the address-space switch mechanism are entirely
  machine-dependent — this is the most target-specific pair after boot. Preserve exactly the
  *return-value protocol* (0 first, nonzero on resume) and the address-space-switch-then-jump
  semantics; `label_t`'s size must match the BESM-6 callee-saved set. See §1 (`label_t`).

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
- **BESM-6 notes.** Replace `hlt` with the BESM-6 wait-for-interrupt idiom; keep exporting the
  halt PC as `waitloc` if the clock accounting is retained.

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
- **BESM-6 notes.** Because BESM-6 is **word-addressed with no sub-word load/store** (see
  [Besm6_Data_Representation.md](Besm6_Data_Representation.md)), the "byte" variants must emulate
  byte access by word read-modify-write (six chars per word). The address checks and the
  `nofault`-style recovery must be reimplemented against the BESM-6 memory map and trap handler.

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
- **BESM-6 notes.** Straightforward to port; only the fixed-point scale shift and the buffer
  addressing need adjusting for word addressing.

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
- **BESM-6 notes.** Trivial to port; `len` is in bytes on x86, but on a word-addressed machine
  these become word-count loops (mind the byte/word unit — six chars per word).

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
- **BESM-6 notes.** BESM-6 has no x86 port space; these are replaced by the BESM-6 device
  register / channel access primitives. The driver layer that calls them will itself be rewritten.

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
  user page table `upt`. On BESM-6 it becomes a **no-op**: writing a page register refills the
  corresponding TLB entries in the same instruction, so a stale translation is not a state the
  machine can be in ([Memory_Mapping.md](Memory_Mapping.md#the-registers)).
- **`cli`/`sti`** clear/set the CPU interrupt-enable flag around very short critical sequences
  (programming the PIT in `machdep.c:89-93`, the beeper timer in `dev/sc.c:560-564`). On BESM-6,
  the equivalent global interrupt-disable/enable.

---

## 5. Global variables and data tables defined in `x86.s`

### Contract-level globals (referenced by C)

| symbol | x86.s | C declaration | meaning |
|--------|-------|---------------|---------|
| `u` | 922 (`.set u, U`) | `extern struct user u;` (user.h:101) | the per-process user area, mapped at a fixed virtual address (page 7); holds the kernel stack and per-process state |
| `kend` | 916 | `extern int kend;` (main.c:19, machdep.c:18) | first free physical address after the kernel; used to size the initial process and free core |
| `phymem` | 919 | `extern int phymem;` (machdep.c:18) | physical memory size in pages, found by the boot memory scan; `startup()` frees it into `coremap` |
| `waitloc` | 893 | `extern caddr_t waitloc;` (clock.c:37) | PC of the idle `hlt`; the clock compares the interrupted PC against it to charge idle time |
| `mem` | 926 (`.set mem, 0x40000000`) | (used via macros) | base of the window that maps *all* physical memory into the kernel address space (`PHY` in `utab.c`) |
| `pdir` | 927 (`.set pdir, 0x7ff9a000`) | `extern int pdir[];` (utab.c:14) | virtual address of the page directory; read by `physaddr()` |
| `upt` | 928 (`.set upt, 0x7ff9b000`) | `extern int upt[];` (utab.c:14) | virtual address of the **user page table**; rewritten by `sureg()` to map the current process's text/data/stack |

These last three are the ones with **no BESM-6 counterpart at all**. The machine has no page table in
memory, no page-directory base register and no page walk — the entire mapping is eight write-only
registers — and no 32-bit window can hold 512 Kwords of physical memory. `kernel/besm6.S` reserves
`pdir`/`upt`/`mem` as placeholder arrays only so `utab.c` links; what replaces them is a per-process
**shadow** page table in kernel memory, written out to РП/РЗ with `рег`. See
[Memory_Mapping.md](Memory_Mapping.md#what-this-means-for-the-v7-kernel).

`pl` is also contract-*adjacent*: it is a private cell ([x86.s:890](../kernel/x86.s)), but its
value at trap time is captured into `struct trap.pl` (reg.h:24) and tested by `BASEPRI()`
(param.h:145) in `clock.c`.

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
| `bcopy`, `bzero` | **trivial** — word-count copy/zero loops (mind 6 chars/word) |
| `spl0`…`spl7`, `splx` | **easy** — keep a level in `pl`, map to the BESM-6 interrupt mask, return old level; add deferral only if masking-by-class is unavailable |
| `addupc` | **easy** — same histogram logic, adjust fixed-point scale and word addressing |
| `cli`, `sti` | **easy** — global interrupt disable/enable |
| `fubyte`/`fuword`/`subyte`/`suword`, `copyin`/`copyout` | **moderate** — need a new `nofault`-style expected-fault recovery in the BESM-6 trap handler; byte variants emulate sub-word access via word RMW |
| `save`, `resume` | **hard, fully machine-dependent** — resize `label_t` to the BESM-6 callee-saved set, reimplement address-space switch + longjmp; **preserve the 0/nonzero return protocol** |
| `idle` | **moderate** — BESM-6 wait-for-interrupt; keep publishing the halt PC as `waitloc` if clock accounting stays |
| `savfp`/`restfp`/`stst` | **rewrite** — BESM-6 float context (non-IEEE-754), no 108-byte frame; may shrink to near-nothing |
| `inb`/`outb`/`insw`/`outsw` | **replace** — BESM-6 has no x86 port space; use channel/device-register access; driver layer rewritten anyway |
| `ld_cr0/2/3`, `invd` | **replace/drop** — target-specific status registers; `invd` becomes the MMU's translation-invalidate (or a no-op) |
| `_start`, trap/IRQ/syscall dispatch (§2–3) | **rewrite wholesale** — BESM-6 bring-up, its own trap and extracode (`$77 N`) mechanism; reproduce the *outputs* (`kend`, `phymem`, mapped u-area/stack, live trap vector, first user-mode entry) and the *shape* (`struct trap` frame, `nofault` check, `runrun` reschedule) |

Remember the calling-convention shift when re-coding any of these: BESM-6 passes arguments in
direct order with the last argument in the accumulator, `r14` = negative arg count, `r13` =
return address (see [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)), and every C
scalar occupies one 48-bit word (see [Besm6_Data_Representation.md](Besm6_Data_Representation.md)).
