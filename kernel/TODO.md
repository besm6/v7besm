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
| 36 | the shifting copy: the half of the byte path task 28 could not reach | medium, high risk |
| 37 | `mdvol[]` is filled only by a READ, so a pack that is only ever written is stamped with another drive's label | small |

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

---

## 36. The shifting copy

**Where.** `copyinb`/`copyoutb` in [ucopy.c](ucopy.c), and whatever machine assist they end up
calling.

**What is left.** Task 28 gave `iomove()` a bulk path, and it reaches the **in-phase** case only —
both pointers standing on the same byte of their respective words, so that after a partial leading
word the middle is whole words on both sides. Out of phase, every word of the transfer straddles
two on the other side, and the copy is still one `fubyte`/`subyte` per byte: a `useracc()` range
walk and a mode-toggle bracket for six bits of payload.

**How much it is.** `nioshift` (systm.h; the deleted `libtest.ini.in` printed it on every run) said **94,805
bytes** of `libtest`'s 1,253,598, against 871 left on the in-phase arm and 1,157,922 through the
bulk path. So this is now the *whole* of what `iomove()` still moves a byte at a time, and it is
7.6% of the traffic and a much larger share of the time. `session` is 2,894 bytes, unchanged by
task 28 and unchanged by anything since.

**Where it comes from**, which is worth knowing before optimising it: the kernel-side pointer is
`(caddr_t)bp->b_addr + on` with `on = u_offset % BSIZE`, so its phase walks with the file offset,
while the user's buffer stays put. `read(fd, buf, 100)` in a loop lands here on every call after
the first. A program whose transfers are multiples of six never does.

**What to do.** Two candidates, and the first is much the cheaper:

1. **Word-at-a-time in C, through `fuword`/`suword`.** For `copyinb`, read whole user words with
   `fuword` and split them in C — one `useracc()` per six bytes instead of six. For `copyoutb`,
   assemble each user word from the kernel bytes and `suword` it, reading the old word back with
   `fuword` only for the two partial end words. Roughly 3–6× on the same arm, no assembly, and the
   masks are the ones `ucopy.c` already reasons about.
2. **A funnel shift in `usermem.S`.** `asx`/`asn` shift `[A, Y]` as one 96-bit quantity and `yta`
   reads `Y` back, so a two-instruction shift-and-carry per word is available; it is used nowhere
   in this kernel today. Faster than (1) and the only way to get the mode-toggle bracket down to
   one per word — but it is assembly, in the file whose header explains why it stays word-only, and
   `copyinb`/`copyoutb` would have to hand it the two phases explicitly rather than mask them away.

Do (1) first and re-read `nioshift`; (2) is only worth it if the counter says so afterwards.

**How to verify.** [test/umem](test/umem.c) **already covers this**, and covering it is why its
matrix is 6 × 6 and not 6: every unequal `(ku, kk)` pair is an out-of-phase transfer, at lengths
0–13 and at 200, with the whole destination window compared against an independent oracle so that
a shift that lands one byte out is caught wherever it lands. The `.ini` header lists the mutations
that made it bite. So this task's bite test is written; what a new implementation must do is fail
`umem` when it is wrong, which the existing mutation list already demonstrates it does.

**Size.** Medium for (1), and higher risk than it looks — this is the same routine that produced
one silent data-corruption bug already, and the reason `umem` exists.


---

## 37. `mdvol[]` is filled only by a read

**Where.** `mdvol[]` in [dev/md.c](dev/md.c) — filled at `md.c:552` inside
`if (bp->b_flags & B_READ)`, stamped into the sector header at `md.c:385` and `md.c:391`
inside `if ((bp->b_flags & B_READ) == 0)`.

**What is wrong.** The service-word buffer is the **controller's**, so the volume mark a write
puts on the platter has to come from somewhere the driver keeps **per drive** — which is what
`mdvol[]` is for, and the comment above it says so. But it is filled from a completed *read*
and from nothing else, and `0` is treated as "not seen yet" and left alone:

```c
if (mdvol[dev] != 0)
    sys[track * MDSYSHALF + 1] = mdvol[dev];
```

Leaving it alone does not leave the header blank. It leaves whatever the **last read of any
drive on that controller** put in the controller's buffer. So a pack that is written without
ever being read is stamped with **another drive's volume number**.

**How it was found, and why not before.** Task C7. `tar cf /dev/rmd1` is the first program in
this tree that writes a pack it never reads: `mkfs` reads the last block before writing the
first (its own end-of-volume probe), `fsck` reads everything it repairs, and `dd` is pointed at
a device the caller has usually just read. `kernel/test/mkfs`'s oracle 1 was written for exactly
this class of defect and passes only because of that probe. `kernel/test/tar` reproduces it in
one line:

```
attach -n md01 scratch3100.disk      # SIMH formats it, volume 3100 throughout
tar cfb /dev/rmd1 6 tree             # writes from block 0, never reads
b6fsutil -S scratch3100.disk out     # "scratch3100.disk: volume 3099 -> out (flat)"
```

3099 is the **root** pack's number, off drive 0.

**What it costs today.** Nothing that has been noticed: `b6fsutil`'s `from_simh()` validates the
magic *mark* and the per-half-zone self-address, and both are right — only the volume *number*
beside the mark is another pack's. A container written this way converts, fscks and boots. What
it breaks is the one thing the number is for: telling two packs apart.

**How to fix it.** Either read the label before the first write to a drive (`mdopen()` is the
obvious place, and it currently does nothing but bound the minor), or carry the number the way
SIMH does and take it from the drive rather than the controller. The first is smaller and needs
no new state; it costs one exchange per open of a drive that has not been read.

**What is asserted meanwhile.** Nothing: `kernel/test/run-tar.sh` required the **wrong** number
and said why, so that the day this was fixed the check would fail and
has to be tightened to 3100, which is what stops the deferral being forgotten.

**Size.** Small.

