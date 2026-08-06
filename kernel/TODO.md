# The BESM-6 Unix port: what is left

The work plan. **[README.md](README.md) is the reference** — where the port stands, the design it
settled on, the five hardware rules, the u-area invariant, what a standalone SIMH test costs to get
right, the gotchas, and the consequences deliberately accepted. Read it before starting any task
below; nothing here repeats it.

Each task leaves the tree building (`cd kernel && make`) and the suite passing (`ctest -L kernel`).
Verification is under SIMH via `test/*.ini` — `b6sim` runs a user `a.out` with no kernel underneath
and cannot exercise any of this. `test/mmutest` is the model to copy, and every MMU test runs with
`set mmu cache`; README.md's "Writing a standalone SIMH test" is the rest of that story, including
why `besm6.o` cannot go into one.

**Tasks 1 through 28 are done and their writeups have been removed**; the design they settled on is
README.md, and how each turned out is in the source comments and in [../doc/](../doc/). The
numbering is **left as it was** — task numbers are cited from the sources and from `doc/`.

The tasks left are small, independent, and were deferred deliberately.

| | task | size |
|---|---|---|
| 32 | `profil()`: implement `addupc()` or make it fail | small; the decision is the task |
| 33 | `ptrace` single-step | small now, blocked after |
| 34 | the `int` ↔ pointer audit | open-ended |

---

## 32. `profil()`: implement `addupc()` or make it fail

`addupc()` is a stub ([besm6.S](besm6.S):773), so `profil(2)` is accepted, records nothing, and says
nothing. Its three callers ([clock.c](clock.c), [syscall.c](syscall.c), [trap.c](trap.c)) are all
guarded by `u.u_prof.pr_scale`, so today the stub is never reached with work to do.

**Decide the direction first.** Nothing in userland profiles: there is no `monitor()` or `mcount()`
in [../lib/libc/](../lib/libc/), `b6cc` has no `-p`, and no `prof(1)` is ported. So the cheap honest
move is to make `profil()` fail — `EINVAL` for a non-zero scale — and delete the stub, leaving one
line saying what would have to exist first. Implementing it is only worth doing behind a libc
`monitor()` and a host-side `prof`.

**If it is implemented:** v7's `addupc` indexes an array of 16-bit shorts by a byte offset, scaling
`pc` by a 16-bit binary fraction. Neither survives: the profile buffer here is an array of **words**,
and the bucket index is a word index, so `pr_scale`'s definition and `<sys/user.h>`'s `struct uprof`
have to be restated before any code is written. The update also lands in the *user's* buffer while
the kernel is unmapped, so it goes through `fuword`/`suword`, not a store — and it happens at clock
interrupt, so it must not fault.

**Size.** Small either way; the decision is the whole task.

---

## 33. `ptrace` single-step

Marked `TODO 33` at [sig.c](sig.c) (cases 6 and 9), [trap.c](trap.c) (the `GRP_BREAKPOINT` arm) and
`GRP_BREAKPOINT` in [../include/sys/besm6dev.h](../include/sys/besm6dev.h).

**Why it is not a flag bit.** v7's `PT_STEP` sets the PDP-11 T-bit and the hardware traps after one
instruction. This machine has no such bit. What it has is a pair of debug registers
([../doc/Memory_Mapping.md](../doc/Memory_Mapping.md) §13): **ИБП** (`M[034]`, КРА) is an *execute
breakpoint* — one address — and **ДВП** (`M[035]`, ЗПСЧ) is a data watchpoint with `PSW_WRITE_WATCH`
selecting write- or read-match. They raise ГРП bits 12, 16 and 17, and they match the *tagged*
address, so they follow the current mapping mode.

A breakpoint register is not a single-step: stepping with it means decoding the instruction at the
resume PC, computing the successor and arming `M[034]` on it — and a conditional branch has two
successors against one register. So `PT_STEP` needs either an instruction decoder in the kernel or a
different contract with the debugger.

**Today it is worse than unimplemented: it lies.** `procxmt()` case 9 falls through to case 7 and
resumes the process with no trap armed, so a debugger waits forever for a stop that will never come.

**Do this much now** (small, and independent of everything above): make case 9 return `EIO` rather
than fall through, and say so in one comment at each of the three sites.

**The rest is blocked on a user.** No debugger is ported — no `adb`, no `sdb` — so there is nothing
to hold an implementation to. When one arrives, the design question to settle first is whether to
offer hardware breakpoints as their own `ptrace` request (`M[034]` armed at an address, re-armed by
`procxmt()` after each match, since there is no flag to clear) or to keep v7's ABI and pay for the
decoder.

**Size.** Small now; the full version is a task of its own and should be re-scoped when a debugger
exists.

---

## 34. The `int` ↔ pointer audit

Both bugs `libtest` found on its first run were one pattern in two disguises — **v7 packs flags into
spare low bits of addresses, and this machine has none** — and both sat in code every other test
walked straight past. The rule and the two worked examples are in README.md's gotchas. What is *not*
done is the sweep.

**What to look for.** Two distinct mistakes:

* **A mask on `(int)ptr`.** A `caddr_t` is a fat pointer — marker in bit 48, byte offset in bits
  47–45 — so `(int)cp & MASK` reaches bits of the *word* address and cannot see the byte offset at
  all. Grep for `& 1`, `& ~1`, `& (NBPW - 1)` and `& CROUND`-shaped masks. `prim.c`'s clists and
  `sh`'s parse nodes are the two already fixed.
**One boundary this rule does NOT cross, found by task C8.** "An `int` and a pointer are the same
word here" is true of a *run-time* conversion and false of a **static initializer**: the compiler
folds an address constant from `&lvalue`, an array name and *address ± constant*, and from no
**cast** at all, so `(int)proc` and `(int *)proc` are both rejected outright — and a non-`void`
pointer field will not take a bare `proc` either. `struct ksym`'s address field in
[kctl.c](kctl.c) is a `void *` for that reason, which makes it fat and means every read of it
goes through `ptrword()`. [../doc/Besm6_Data_Representation.md](../doc/Besm6_Data_Representation.md)
§7 carries the general rule now. It is not a hazard for the sweep below — it fails loudly at
compile time — but it is the answer to "why is this a `void *`" and it will be asked.

* **A pointer fabricated from a small integer.** `u.u_dirp = (caddr_t)u.u_arg[0]`
  ([syscall.c](syscall.c)) is fine — that cast is a silent `COPY`, so the caller's marker and byte
  offset survive and `namei()`'s `fubyte(u.u_dirp++)` is right. Everywhere *else* has to be checked
  one site at a time: `(caddr_t)ipc.ip_addr` in [sig.c](sig.c) (four sites) is a `ptrace` client's
  address and is only right if a word address is what the ABI promises, which nothing states today.
  An `int` and a pointer are the same word here and `b6cc` converts between them without a word.

**Deliverable.** A pass over `kernel/*.c` and `kernel/dev/*.c` recording, per site, which of the two
things the integer is; the fixes; and the ABI sentence for `ptrace`'s `ip_addr` written down in
[../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md).

**How to verify.** Neither of the two known bugs reproduced under `b6sim`, where the host serves the
system call and the kernel's copy never runs, and neither was found by review — both were found by a
*user program* doing something ordinary. So the check on this task is a user-level program that
exercises the site, run under `libtest`, not an inspection.

**Size.** Open-ended; do it in one sitting per file and stop when the grep is clean.
