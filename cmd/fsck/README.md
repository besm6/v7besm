# `fsck`, and what a second opinion is worth

Task C4d. `/etc/fsck` is on the image, so the machine can repair a filesystem rather than
only measure ([`df`](../df/README.md)), move (`dd`) or make ([`mkfs`](../mkfs/README.md))
one. This file is the account of what the port taught; the program is documented in its own
header and in [fsck.1m](fsck.1m), [../README.md](../README.md) is the porting recipe, and
[../df/README.md](../df/README.md) is the account of the four alignment conditions a raw
transfer obeys. **Read that one first** — nothing here repeats it.

The short version of the port is that 1,684 lines came out as about 1,270 and got *faster
to read*. Most of the deletion is one thing: v7's `fsck` carries an entire second
implementation of itself for the case where its tables do not fit in memory, and the case
cannot arise here.

---

## 1. The best oracle in the tree, and the four times it disagreed

`cmd/TODO.md` picked this task out for a reason: `cmd/fsutil/check.cpp` already implements
the same checks on the host, in C++, sharing not one line with the guest. So a damaged
filesystem can be handed to both, and **every case they disagree on is a bug in one of
them**. That is the whole design of [test/](test/): `b6fsutil -D` breaks an image, `fsck -y`
repairs it under `b6sim`, and `b6fsutil -c` has to find nothing afterwards.

Four disagreements turned up. Three were `fsck`'s fault and are fixed; the fourth was
neither tool's, and it took a change to the kernel to settle.

### `s_tinode` was one too high, on every clean filesystem this system ever made

v7 compares the superblock's free-inode counter against `imax - n_files`: every i-number in
the list that is not in use. **Inode 1 is not one of them.** `ialloc()` refuses to hand out
anything below `ROOTINO` ([kernel/alloc.c](../../kernel/alloc.c)), so it is a slot that
exists and can never be filled — and both `mkfs` and `create.cpp` seed the field as
`ninodes - 2`, "everything but inode 1, which cannot be allocated, and the root, which is in
use". Carried unchanged, v7's arithmetic makes `fsck` complain about every filesystem this
port has ever produced. It was the first thing the very first run said.

### `s_magic` was never looked at

v7's `setup()` checks that `s_isize` and `s_fsize` are consistent with each other and
nothing else — it had nothing else to check, v7's `struct filsys` having no magic number.
This port's has four geometry words, `sbcheck()` (`kernel/alloc.c`) refuses to *mount* a
filesystem that fails them, and `check.cpp` refuses to *check* one. A `fsck` that walked it
anyway would have been the one program of the three with an opinion of its own, so it now
refuses with `sbcheck()`'s own wording. `cmd_fsck_notafs` is that case.

### A reconnected file could have been unreachable by name

`mkentry()` writes the orphan's i-number into a `lost+found` slot as decimal digits and
terminates it — leaving whatever was in the rest of `d_name`. `namei()`
([kernel/nami.c](../../kernel/nami.c)) compares all `DIRSIZ` bytes against a zero-padded
`u_dbuf`, so a slot that had once held a longer name would produce an entry `ls` shows and
`open` cannot find. It happens to be harmless in v7 only because `mklost+found` leaves the
slots zeroed. The name is zeroed here before the digits go in.

### And one that is neither tool's: the two counters nothing maintained

`s_tfree` and `s_tinode` were maintained by **nothing** in this system. `mkfs` set them,
`b6fsutil` set them, and the kernel touched neither — `grep s_tfree kernel/*.c` found
nothing, and [include/sys/filsys.h](../../include/sys/filsys.h) said they survived only
because `mkfs` and `fsck` were going to be ported. So on any filesystem that had been
*written* to they were stale by construction, and v7's offer to `FIX` them was noise that
fired on every check of a live root and was undone by the next write.

For task C4d they were therefore a **note**, asked about never, and `check.cpp` made the same
call for `s_tfree` and did not look at `s_tinode` at all — the tree's one deliberate
disagreement, marked `hostblind` in [test/](test/) so that it was stated out loud rather than
hidden.

**That was a symptom of a hole in the kernel, and the kernel is where it got fixed.**
`alloc()`, `free()`, `ialloc()` and `ifree()` now keep both counters, at the same four points
and in the same direction as `cmd/fsutil/alloc.cpp` had been keeping them all along —
RetroBSD's `sys/kernel/ufs_alloc.c` does the same, and v7 is alone in not. Two consequences
here: `fsck` offers a `FIX` for either, as v7 does, on this port's `imax - n_files - 1`
arithmetic; and `check.cpp` **faults** an image on either, `s_tinode` included, so the
disagreement is gone and `tcounts` is an ordinary repair case. Two ways of noticing one
thing, which is what the rest of this section is about.

Worth keeping in view: nothing in the kernel *acts* on either counter, so `sbcheck()`
deliberately does not police them. A wrong total is a filesystem to check, not a filesystem
to refuse to mount.

**What else the two do not both check** is worth writing down, because "require both to
report the same thing" is only true of the verdict. `check.cpp` looks for five things `fsck`
does not: `.` and `..` present and pointing where they should, a directory loop as an error
in itself, a negative `di_size`, a size needing more blocks than `NADDR`/`NINDIR` can
address, and a free inode with a non-zero link count. `fsck` does three things `check.cpp`
does not: it repairs, it *adjusts* a link count rather than merely reporting it, and it
salvages a free list. Neither list is a defect; they are two tools with different jobs.

**Task C4e did not shorten that list, which is worth saying because this file used to
predict that it would.** `icheck` and `dcheck` are now on the image and between them they
cover the *block accounting* and the *link counts* — both of which `fsck` already had. The
three checks that are still `check.cpp`'s alone are the three that are about a directory
tree's shape rather than its arithmetic: `.` and `..` correct, a loop as an error in itself,
and a size needing more blocks than the inode can address. Nothing in the four programs of
C4e looks at any of them. What C4e *did* add is a fourth guest program with an opinion of
its own about the free list — `icheck -s` — and [../icheck/README.md](../icheck/README.md)
§3 is the account of how it is held to `fsck`'s: byte for byte, by `cmp`.

---

## 2. What a program with 54 Kb of memory looks like when it has enough

Nearly a third of v7's `fsck` is there because a PDP-11 could not hold the tables:

* `main()` sizes an arena with `sbrk` against `MAXDATA`, which is `#ifdef pdp11` /`i386`
  /`vax`/`interdata` — so this file **did not compile**, which is a good way to meet the
  problem;
* when the arena will not hold the maps, `setup()` falls back to a **scratch file**,
  prompting for its name if `-t` did not give one;
* behind that sit a buffer pool, an LRU `search()`, and a second arm in each of `domap()`,
  `dostate()` and `dolncnt()` that reads a map through the pool instead of out of memory;
* and `ginode()` has a second path that sweeps the i-list 11 to 110 blocks at a time into
  the arena.

This machine's drive is 2,000 blocks and a default i-list is one inode per two blocks, so
all four maps together come to about 1,300 words. They are `calloc`'d from the superblock's
own numbers and a failure is a refusal rather than a fallback. Everything above goes with
that decision, and so does `-t`. It is the same shape `mkfs` came out in when its prototype
language went.

Two of the deletions were forced rather than merely available. **The arena cannot be read
into**: `physio()` requires the buffer's word address to be a multiple of `MDTRACK`, and
`sbrk` promises nothing of the sort. And **the i-list sweep cannot happen at all**, a raw
transfer being one whole block at a time. So even a machine that wanted v7's memory
management could not have used it here.

---

## 3. The buffer that cannot be per-level, and cannot be automatic either

`iblock()` reads an indirect block and then loops over it, calling back into the walk. In
`DATA` mode that callback is `dirscan` → `pass2` → `descend` → `ckinode` → **`iblock`
again**: a directory big enough to need an indirect block, containing a subdirectory that
also needs one, has two walks of the same level live at once.

v7 is correct here by accident of storage class — `BUFAREA ib` is an automatic, one per
invocation. That is not available:

* an automatic cannot be `MDALIGN`-aligned, and condition 3 of a raw read requires it;
* and at 515 words a frame, two levels deep inside a `descend` recursion four directories
  deep, it is 4,120 words against a 4,096-word stack that nothing checks
  ([../README.md](../README.md) §6). v7's `fsck` would have blown its own stack on this
  machine before the alignment problem was reached.

One static buffer *per level* is the obvious replacement and is wrong for the reason above.
What is here instead is **one shared buffer, re-fetched on every iteration** —
`getblk(&indblk, blk)` inside the loop, which returns immediately when the block is still
there and re-reads it when the recursion has displaced it. That idiom is not invented for
this: v7's own `dirscan()` already does exactly it to `fileblk`, and for exactly this
reason. Both loops are worth a comment saying so, being the most deletable-looking lines in
the file.

The other 584-word frame, `makefree()`'s `flg[500]` and `addr[500]`, went with the free-list
interleave — `struct filsys` here has no `s_m`/`s_n`, so the list is rebuilt plainly and
descending, which is how `mkfs` builds one. A salvaged volume and a fresh one now have the
same shape.

---

## 4. `hotroot` did not fire, and it is the raw node that matters

v7 decides whether it is checking a mounted filesystem by asking `ustat(2)` and, failing
that, comparing the root's `st_dev` with the device's `st_rdev`. `ustat(2)` does not exist
here — the v7/x86 source had already stubbed it to `-1` — and the comparison **cannot work
for the raw node**, which is the one `fsck` is normally pointed at:
`rootdev` is `makedev(0, 0)` ([kernel/conf.c](../../kernel/conf.c)) and `/dev/rmd0`'s
`st_rdev` is `makedev(3, 0)`. So the test is false exactly when it matters, and a `fsck`
that repaired the live root would have done it with no warning and let the kernel's buffer
cache write stale blocks over the repairs.

The fix is 4.xBSD's `unrawname()`: strip the `r` from the last path component and `stat`
*that*. `/dev/rmd0` → `/dev/md0` → `makedev(0, 0)`. It duplicates no table, which matters —
writing `cdevsw[]`'s raw-to-block pairing into a user program is what `cmd/TODO.md` forbids
— and it answers correctly for `/dev/rmd1`, which is not the root.

---

## 5. Two things about testing a program that repairs

**A refusal to check is not a passing check, and the harness has to know the difference.**
Damaging an image and requiring `fsck` to fix it is only meaningful if the damage was real,
and a damage spec that has drifted out of step with its fixture produces a test that repairs
nothing and passes. So every case here asserts that **`b6fsutil -c` fails before `fsck`
runs** — the other implementation, agreeing that there is something to do — and that it
succeeds afterwards. `run-fsck-test.sh` lists all five assertions; the fifth is that a
second `fsck` finds nothing, because a repair that is not idempotent did not finish.

**And `b6fsutil` had to learn to break things.** There was no way to corrupt an image from
its command line; the recipes existed only as C++ inside `cmd/fsutil/test/check_test.cpp`,
where the guest program cannot reach them. The `-D` verb is the answer, and the one design
decision in it worth repeating is that its targets are **symbolic** — `sb.nfree`,
`i5.nlink`, `e3.2`, `b12.0` — resolved through the tool's own word-offset tables. A shell
script could compute the byte offset of inode 5's link count itself; then the on-disk layout
would have a second home in a test script, which is exactly what `params.cpp` exists to
prevent. Two rules come with it, both in `damage.h`: it writes the raw word without
`to_word()`'s range check, because a corruption tool whose worst case is a legal value is
not much of one; and it never `sync()`s, or a damaged superblock would be silently rewritten
from the in-core mirror on the way out.

---

## 6. The one thing only a boot can say

`kernel/test/fsck` (volumes 3091 root, 3092 scratch) exists for the device — a repair is a
scatter of small *writes*, and phase 6 lays down the entire free list of a 2,000-block
volume, which is the longest run of raw writes anything on this system performs. That much
is `mkfs`'s harness pointed at a different question.

What it also does is the one-line statement of the whole task: it checks **the filesystem
the machine is running on**, read-only, and three independent measurements of that volume's
free space — `fsck`'s, `df`'s, and the host's, taken afterwards from outside the machine —
have to agree.

That check is sound only for as long as nothing allocates a block after `fsck`'s `sync(2)`,
which is a property of the *order* of `kernel/test/fsck.sh` and not of any program. The
warning is in that file's header in the imperative, and the failure it guards against would
be an intermittent `N BLK(S) MISSING` — so `run-fsck.sh` names the cause in its diagnostic
rather than leaving the next person to find it.

---

## Size

`const 125, text 6323, data 587, bss 3807` — **10,842 words** of the 28,672, plus about
1,300 of heap for a full drive. It is the largest program on the image, ahead of `quot`'s
9,912, and it is not close to the ceiling: `cmd/TODO.md` held the option of splitting it as
v7 split `icheck`/`dcheck` in reserve, and that is not needed. Of the `bss`, 2,560 words are
the four aligned block buffers and about 1,030 are stdio's.

The stack is the ceiling that mattered, and nothing checks it: the two frames removed above
came to 1,099 words between them, against 4,096 for the whole stack.
