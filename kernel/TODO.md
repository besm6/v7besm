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

31–36 are small, independent, and were deferred deliberately.

| | task | size |
|---|---|---|
| 31 | the kernel-stack depth check | small |
| 32 | `profil()`: implement `addupc()` or make it fail | small; the decision is the task |
| 33 | `ptrace` single-step | small now, blocked after |
| 34 | the `int` ↔ pointer audit | open-ended |
| 35 | the dropped `send` character is fixed and `console`/`edit` are back; what is left is whether the resource lock and the paced `send` still buy anything | small, and it is a measurement |
| 36 | the shifting copy: the half of the byte path task 28 could not reach | medium, high risk |
| 37 | `mdvol[]` is filled only by a READ, so a pack that is only ever written is stamped with another drive's label | small |
| 39 | the 4,096-word user stack: `USTKPAGE` 28 → 24, and why C9b argues against it | small change, wide blast radius |

---

## 39. The user stack is 4,096 words — and the image ceiling is now the tighter one

**Where.** `USTKPAGE 28` in [../include/sys/param.h](../include/sys/param.h). `estabur()`
([utab.c](utab.c)) derives *both* user ceilings from it: 28 pages of `const+text+data+bss` below
it and 4 pages of stack from `070000` up. The two move in opposite directions, which is the
whole of this task.

**Why it was raised.** Task C9a ([../cmd/cpp/README.md](../cmd/cpp/README.md), "Building for the
BESM-6") is the one program on this image whose stack the split bounds tightly enough to cost
behaviour rather than headroom. C11 §6.10.3.1 macro-argument prescanning is a recursion the
*input* drives, and the measured chain — `main` 41 + `process_directives` 372 + `scan_token` 656
+ `lookup_token` 11 resident, then 1,227 words per nesting level and another 1,106 for the inner
macro's argument collection — fits **one** level in 4,096 and not two. So `cpp` carries
`MAXARGDEPTH 1` and substitutes a deeper argument raw. It is honest, warned about and almost
always invisible, but it is a real subset of the language.

**C9b is evidence AGAINST the change, and this is the part to read before doing it.** That task
predicted `as` and `ld` would want the bigger stack too. Neither does, and `ld` would be actively
hurt:

* **`ld` has no recursion at all** and not one local array. Its deepest chain —
  `pass2` → `relocate_file` → `relocate_object` → `relocate_segment` → `relocate_halfword` →
  `lookup_local` — is **578 words** of the 4,096 ([../cmd/ld/README.md](../cmd/ld/README.md) has
  the frames). What it is short of is *image*: it links at **23,951** words of 28,672, and the
  ~4,700 left are the heap budget for twelve stdio buffers. `USTKPAGE 24` would cut the ceiling
  to 24,576 and leave it 625 words for a heap that needs ~2,050. **`/usr/bin/ld` would stop
  linking.**
* **`as` needed only a bound of its own**, `MAXEXPRDEPTH`, on the one recursion its input drives
  — the same move as `grep`'s `MAXDEPTH`. It is 554 words resident plus 104 a level, so 20 levels
  fit in 3,130 with room to spare, and no real input nests parentheses at all.

So the trade is now explicit: **`USTKPAGE 24` buys `cpp` one more level of macro-argument
nesting and costs `ld` the ability to run.** Anyone taking this task has to shrink `ld` first —
its `NCONST`/`NSYM` profile is where the words are — or pick 26 rather than 24 and re-measure
both.

**What to change**, if it is still wanted: `USTKPAGE` 24 gives 8,192 words of stack and 24,576
of image. **What moves with it**, and none of it is computed: `cmd/sim`'s `STACK_BASE`
([../cmd/sim/besm6_arch.h](../cmd/sim/besm6_arch.h)) — b6sim seeds `M15` at `070000` and its
memory ends at `0100000`, so the two worlds must agree or a program that passes under the
simulator faults under the kernel; the `28672` literals in
[../scripts/BesmCross.cmake](../scripts/BesmCross.cmake) and `lib/test/CMakeLists.txt`, which are
what `rootfs_<name>_size` checks; `scripts/check-size.sh`'s header; `cmd/README.md` §6 and
`../doc/Memory_Mapping.md`. **The `32767` ceiling does not move** — that is the 15-bit pointer
and has nothing to do with this split.

**How to know it worked.** Raise `cpp`'s `MAXARGDEPTH` to 2, rebuild, and check that
`rootfs_cpp_deep` still agrees with the host and that `ID(ID(ID(4)))` now expands — and that
`rootfs_ld_size` and the four `rootfs_ld_link*` cases still pass, which is the half this task
did not originally have.

---

## 31. The kernel-stack depth check

There is **no guard page and none is possible.** `r15` grows up from ≈ `074214` to `0100000`, and
past that a 15-bit address wraps to 0 — into the interrupt vectors. The kernel runs unmapped, so
neither РП nor РЗ applies to it; the only mechanisms are a software depth check or the simulator.
Task 25a made the wrap unreachable by any measured path, but two one-line checks were left unwritten:

```c
if ((int)&local >= UBASE + USIZE)   panic("kstack");   /* in sleep() — the one the geometry needs */
if ((int)&local >= 0100000 - MARGIN) panic("kstack");  /* the wrap */
```

The first is the important one: `sleep()` (and `swtch()`) is exactly where a frame in the overflow
page is silently lost, because `uflush()` copies at most `USIZE` words — it clamps there, task 30
having left that threshold exactly where it was — and another process then runs deep on the same
physical page. There is no fault and no diagnostic — see README.md's consequences list.

**Decide first whether it earns its keep**, because SIMH already observes both without kernel code: a
write watchpoint on the overflow page (`break -w 0176000-0177777`) fires only on an *unmapped* store,
i.e. only on a kernel stack frame, because `mmu_store()` ORs `0100000` into the address before
`sim_brk_test`; and `break <resume>; ex M17` samples the depth at every switch. The argument for the
runtime check is that it fires on a real workload nobody is watching, and it costs one comparison per
sleep.

**How to verify.** A standalone test that forges a deep frame and sleeps in it must panic; without
that, the check is decorative. A test that cannot fail proves nothing about the geometry.

**Size.** Small.

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

## 35. What the paced `send` is still buying

**The character drop is found and fixed, and it was one bug on each side of the boundary** —
which is why neither candidate mechanism in the old text ever explained all of it.

*The guest half.* `scintr()` ([dev/sc.c](dev/sc.c)) skipped a Consul that was not open
**without dismissing its ПРП bits**, and those bits are cleared there and nowhere else. The
processor re-tests ПРП before every instruction, so a "printing finished" that arrived just
after `ttyclose()` zeroed `t_state` — and `wflushtty()` waits for the queue to drain, not for
the character still in the typewriter — re-raised GRP_SLAVE for ever. That is the one-in-three
`console` wobble and the `edit` byte that appeared to be *gained*: the guest was not losing a
character, it was stalled mid-echo. Intermittent because it turned on whether init's close beat
that one interrupt. `scintr()` now dismisses, and reads, whatever stands for a closed line.

*The simulator half.* `CONSUL_IN[]` in SIMH's `besm6_tty.c` is one character deep and
`consul_receive()` overwrote it every tick regardless of whether the guest had read it, so
anything arriving faster than the guest services ПРП was lost. It now leaves the character in
the line's own queue until `consul_read()` takes it. **This is the bug a real operator meets**,
and `more(1)` was the first program to meet it: an arrow key sends three bytes in one instant,
the middle one went, and the pager rang the bell instead of scrolling.

With both in, `console` and `edit` are **enabled again** and the whole weekly suite has run
100% three times over; `console` measured 6 of 6 where it was 0 of 6 before.

**What is left is an optimisation, not a bug.**

1. **Re-measure the `RESOURCE_LOCK simh_boot`.** It was bought to treat this symptom and has
   never been measured against a paced send, let alone against a fixed one — `test/CMakeLists.txt`
   says so at the `RESOURCE_LOCK` comment. Five full `ctest` runs with and without it, which is
   how it earned its place. It costs about seventy seconds of serial wall clock on the critical
   path of the suite. If it is buying nothing now, the lock and its comment both come out.
2. **Re-measure the `after=`/`delay=` pacing.** Every `send` in `test/` now carries
   `after=20000 delay=20000`, the thirteen single-send files having been brought into line with
   the dialogues. Whether either is still needed after the two fixes is untested; 20000 is a
   number that worked, not a measured minimum. Take them off one file and run it six times.

**How to verify a change here.** Make the fault happen first. For the drop, the reproducer is one
line: `send "\033[B"` with **no** `delay=` at a `--More--` prompt used to arrive as `ESC B` and
ring the bell, and now scrolls. For the storm, `console` six times is the measurement.

---

## 36. The shifting copy

**Where.** `copyinb`/`copyoutb` in [ucopy.c](ucopy.c), and whatever machine assist they end up
calling.

**What is left.** Task 28 gave `iomove()` a bulk path, and it reaches the **in-phase** case only —
both pointers standing on the same byte of their respective words, so that after a partial leading
word the middle is whole words on both sides. Out of phase, every word of the transfer straddles
two on the other side, and the copy is still one `fubyte`/`subyte` per byte: a `useracc()` range
walk and a mode-toggle bracket for six bits of payload.

**How much it is.** `nioshift` (systm.h; `libtest.ini.in` prints it on every run) said **94,805
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

**What is asserted meanwhile.** `kernel/test/run-tar.sh` requires the **wrong** number and says
why, in `kernel/test/fsck.sh`'s `hostblind` style: the day this is fixed that check fails and
has to be tightened to 3100, which is what stops the deferral being forgotten.

**Size.** Small.

