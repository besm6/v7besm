# `icheck`, and three ways to rebuild the same free list

Task C4e. `/etc/icheck` is on the image, along with `/etc/dcheck`, `/etc/ncheck` and
`/etc/clri` — the four one-job tools [`fsck`](../fsck/README.md) grew out of. This file is
the account of what the port taught; the programs are documented in their own headers and
manual pages, [../README.md](../README.md) is the porting recipe, and
[../df/README.md](../df/README.md) is the account of the alignment conditions a raw transfer
obeys. **Read that one first** — nothing here repeats it.

Only `icheck` gets a README, on §10's rule: the other three taught nothing that does not fit
in a paragraph of [../README.md](../README.md)'s task section.

---

## 1. `s_tinode = 0` — the field that was dead in v7 and is load-bearing here

v7's `makefree()` writes both superblock totals as zero:

```c
	sblock.s_tfree = 0;
	sblock.s_tinode = 0;
```

`s_tfree` is then accumulated by `bfree()` on the way down, so it comes out right.
**`s_tinode` never is.** It stays zero, and v7's `icheck` even counts free inodes in
`pass1` (`sblock.s_tinode++`) and then throws the count away, because between the two there
is a second `bread(SUPERB)` that reloads the superblock from disk.

That is harmless in v7, where *nothing* maintains the field — v7's own `filsys(5)` calls
both counters uncurrent. It is not harmless here. `kernel/alloc.c` maintains both at the
four points that change them (task C4d), `cmd/fsutil/check.cpp` **faults** an image on
either, and `fsck` offers a `FIX`. So an `icheck -s` carried over unchanged would produce,
every time, a volume the host's checker rejects and `fsck` wants to repair — a salvage that
leaves the filesystem broken in a new way.

The formula is `fsck`'s, taken unchanged rather than re-reasoned:

```c
	sblock.s_tinode = imax - nfiles - 1;
```

The `- 1` is inode 1. It exists in the i-list and `ialloc()` can never hand it out
([kernel/alloc.c](../../kernel/alloc.c)), so it is neither in use nor free — which is the
same off-by-one that was the very first thing task C4d's first run got wrong.

**The general shape of this is worth keeping.** A field that is inert in the original and
live in the port is invisible to a reading of the diff: nothing in v7's source says
`s_tinode` matters, because there it does not. The way it was caught was the oracle, not
the reading — `cmd_icheck_salvage` requires `b6fsutil -c` to pass on what the guest left
behind, and it did not.

---

## 2. One buffer per level, which is deliberately *not* what `fsck` does

`fsck`'s [README.md](../fsck/README.md) §3 spends a section on why its indirect-block buffer
is **one shared buffer, re-fetched on every iteration** rather than one per level: its
`DATA` walk re-enters `iblock()` through `dirscan` → `pass2` → `descend` → `ckinode`, so two
walks of the same level are live at once and would share a per-level buffer.

`icheck` has one buffer **per level**, and copying `fsck` here would have been wrong twice
over.

* **Nothing calls back into the walk.** `ckindir()` is a closed nested loop: it reads an
  indirect block and either counts its entries or recurses one level deeper. Two walks of a
  level cannot be live at once, so a buffer per level is safe.
* **And the shared idiom would be a disaster on cost.** `getblk()` returns immediately when
  the block is still there, which is what makes it free in `fsck` — but in `icheck` the
  recursion *displaces* the outer block on every inner iteration, so re-fetching would
  re-read the outer indirect block 512 times per double-indirect block. `fsck` never
  notices because its indirect blocks are one level apart in a directory walk; `icheck`'s
  are nested.

So the rule that generalises is not "share it" or "one per level" but a question: **does
anything in the walk re-enter it?** `NLEVEL` buffers, indexed by level, and both files carry
a comment pointing at the other, because each looks like a mistake from the other's side.

The stack is the other half of it. v7's `pass1` declares `daddr_t ind1[NINDIR]`,
`ind2[NINDIR]` and `ind3[NINDIR]` — **1,536 words in one frame** against a 4,096-word stack
that nothing checks ([../README.md](../README.md) §6) — and they could not have been read
into anyway, an automatic not being `MDALIGN`-alignable.

---

## 3. Three implementations of `makefree()`, and a `cmp` that holds them together

Three programs on this image lay a free list down: `mkfs` makes one, `fsck` phase 6
salvages one, and `icheck -s` salvages one. All three now walk `fmax-1` down to `fmin`,
skip what is in use, flush a chain block at `NICFREE` and leave slot 0 of each chain
pointing at the block that holds the previous one. There is no interleave to reproduce —
`struct filsys` here has no `s_m`/`s_n`, which is what deleted `makefree()`'s
`flg[500] + adr[500]`, 584 words of frame — so *a salvaged volume and a fresh one have the
same shape*.

That is a property worth asserting rather than believing, and it turns out to be assertable
in the strongest possible way. `cmd_icheck_salvage` salvages one copy of the fixture with
`icheck -s` and another with `fsck -s`, normalises the one word each stamps for itself
(`b6fsutil -D sb.time=0`), and **`cmp`s the two images**. They are byte-identical.

That is C4c's oracle — "an oracle can be byte-exact when the two implementations are
transcriptions of each other" — with the twist that these two are *not* transcriptions of
each other: they were written separately from the same v7 ancestor, one of them (`fsck`) a
task earlier. A field diff would say the fields it was taught to say; `cmp` says
everything, including `s_ninode`, the tail of `s_free[]` and the interior of every chain
block.

The case also runs the weaker version first — `fsck -n` over what `icheck -s` wrote, which
must find nothing — because that one localises a failure. If `cmp` fails and `fsck -n` is
clean, the disagreement is cosmetic; if both fail, one of the two rebuilders is wrong.

---

## 4. `clri` is the one program whose success is the checker failing

Every case in [../fsck/test/](../fsck/test/) is shaped the same way: break the image, run
the guest, require `b6fsutil -c` to pass. `clri` inverts it. What it does is *make* a
filesystem inconsistent — a cleared i-node with a directory entry still naming it — and a
run that left `b6fsutil -c` happy would be a run that did nothing.

`cmd_icheck_cleared` is written that way round, and it is also the case where the four
programs of this task meet: `clri` throws an i-node away, `icheck` reports its blocks as
`missing`, `dcheck` reports the entry with no links, and the host's checker faults the
volume on all three counts. `clri.1m`'s DESCRIPTION says exactly this in prose ("any blocks
in the affected file will show up as `missing` in an `icheck`"); the case is that sentence
executed.

It lives in `cmd/icheck/test` rather than `cmd/clri/test` for a build reason worth knowing:
**`make test` builds `build_tests` and nothing else**, and a test directory can only hang a
program on that target if the program's target already exists. `cmd/clri` and `cmd/dcheck`
are added to the top-level `CMakeLists.txt` before `cmd/icheck` (they sort earlier), so
neither can reach `b6prog_icheck`. Anything that needs several of the four goes in the
last-configured one's directory.

---

## 5. What is *not* asserted, and it is half the task

Task C4e has **no SIMH test**. Everything above runs under `b6sim`, whose `read(2)` and
`write(2)` are the host's — so **none of the five conditions of the raw path exists in any
of it**. A green `cmd_icheck_*` says the arithmetic is right and says nothing whatever about
the device, and `clri` and `icheck -s` are the first programs since `mkfs` and `fsck` to
write one.

That is a deliberate deferral rather than an oversight, and [../TODO.md](../TODO.md) carries
it as this task's one named loose end, with what it would take to close it: a test on
`kernel/test/fsck`'s shape at volumes 3093 (root) and 3094 (scratch), with the scratch pack
attached *without* `-n`, `clri` and `icheck -s` pointed at `/dev/rmd1`, and a read-only pass
over the live root last. Two things that test would have to know before it is written:

* **`icheck -s` and `clri` must never be pointed at `/dev/rmd0`.** Both stop the machine on
  a hot root by design — `***** BOOT UNIX (NO SYNC!) *****` and `pause()` forever — so the
  symptom would be a 1,800-second ctest timeout with no diagnostic at all.
* **Anything that measures the mounted root goes last, after the `sync`, writing only to
  `/dev/console`**, which is [kernel/test/fsck.sh](../../kernel/test/fsck.sh)'s rule in the
  imperative and for its reason: a `>>/tmp/…` below that line allocates a block after
  `icheck`'s `sync(2)` and turns `missing` non-zero in some runs and not others.

---

## Size

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `icheck` | 92 | 3,725 | 305 | 4,143 | **8,265** |
| `ncheck` | 86 | 3,468 | 215 | 4,228 | **7,997** |
| `dcheck` | 84 | 3,222 | 213 | 4,136 | **7,655** |
| `clri` | 86 | 3,021 | 219 | 2,575 | **5,901** |

Of the 28,672 words a program has. About 1,030 words of each `bss` is stdio's and 2,560 of
`icheck`'s is the five aligned block buffers; `clri` has two buffers and is 1,568 words
smaller for it. On top of that comes heap sized from the superblock — `icheck`'s block map
is `(fmax-fmin)/8` bytes (42 words on a full drive), `dcheck`'s entry counts one word per
inode (about 1,024), `ncheck`'s directory table five words per inode (about 5,125).

**`ncheck`'s table is the number worth keeping in view.** v7's fixed `htab[2503]` would have
been **12,515 words** of permanent bss — 44% of the address space — on a machine whose i-list
can never exceed about 1,024 entries, so three fifths of it was unreachable by construction.
Sized from the superblock instead, it is heap, it scales with the volume, and it refuses at
startup rather than dying partway through with `out of core -- increase HSIZE`.
