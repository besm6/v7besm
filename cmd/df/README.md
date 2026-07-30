# df, and the four rules a raw read obeys

`/bin/df`, `/bin/du` and `/etc/quot` are task C4a ([../TODO.md](../TODO.md)) — the first three
programs on this image that can say anything about the filesystem they live on. Everything
before them could *change* the store; these measure it.

The C11 pass over each source is described in that file's own header and is not repeated here.
**What the port taught is below, and only the first section is about `df`** — the rest belongs to
whatever reads a device next, which is the whole of the rest of C4.

## A raw transfer has four conditions, and three of them fail with `EFAULT`

`cmd/TODO.md` says of task C4 that "the raw devices these need are already on the image —
`/dev/rmd0` and `/dev/rmb0`". They are, and they are the right ones. But `open`/`lseek`/`read`
on `/dev/rmd0` is not the v7 call sequence it looks like: it goes
[`physio()`](../../kernel/dev/bio.c) → [`mdstrategy()`](../../kernel/dev/md.c), and between them
those two impose four conditions that a PDP-11 program had no reason to think about.

| | condition | who enforces it | what a breach looks like |
|---|---|---|---|
| 1 | the buffer starts at **byte #0 of a word** | `physio()`, `ptrbyte(u.u_base) != 5` | `read` returns −1, `errno` = `EFAULT` |
| 2 | the count is a whole number of **`BSIZE`** | `physio()` (`% NBPW`) and `mdstrategy()` (`% MDTRACK`) | the same |
| 3 | the buffer's **word address** is a multiple of `MDTRACK` | `mdstrategy()`, `bufpaddr(bp) & (MDTRACK-1)` | the same |
| 4 | the seek offset is a multiple of **`BSIZE`** | nobody — `physio()` computes `wtodb(btow(u_offset))`, which **truncates** | the wrong block is read and nothing says so |

`MDTRACK` is `BSIZEW`, 512 words: it is the half-zone, which is the finest unit the disk's
track-address command and the `CW_PAGE`/`DISK_HALFPAGE` control-word pair can name, and there is
nowhere in either to put an offset within one. Condition 4 is the one to fear, being the only
silent one.

**Condition 3 is the one C cannot express.** There is no `_Alignas` reaching 512 words here and
no allocator that promises it. What there is, and what both programs use, is the fact that
**an `int *` is a word address** — `lib/test/memt.c:135` already casts one that way to aim
`/dev/mem` at a variable — so the buffer is an over-sized `bss` array stepped forward at startup:

```c
#define MDALIGN BSIZEW
static int rawbuf[BSIZEW + MDALIGN];
static int *blk;

    blk = rawbuf;
    while ((int)blk % MDALIGN != 0)
        blk++;
```

Aligning the *virtual* address is enough because a page is `PGSZ` words and mapping preserves the
offset within a page, so virtual and physical agree modulo 512 — there is a `_Static_assert` on
`PGSZ % MDALIGN` in both sources saying so.

**One invariant is being leaned on rather than checked**, and it is worth knowing about because
it is not this code's to keep: `physrange()` ([../../kernel/utab.c](../../kernel/utab.c)) also
requires the transfer to land on one contiguous run of *physical* pages, and a 1,024-word `bss`
array crosses a page boundary. Its own comment records that the check "cannot fire today",
`expand()` keeping a process image one run. If a future swapper breaks that, these two programs
are among the things that stop working, and `EFAULT` is how they will say so.

### What obeying the rules cost, and what it bought

It cost `df` nothing and saved it 321 words: v7's `alloc()` read a chain block into a
`struct fblk` **automatic**, which is neither aligned nor a whole block, so it could not have
been read into at all. There is one aligned block buffer, the chain block is read into it, and
`df` reads `df_nfree`/`df_free[]` through a `struct fblk *` cast.

It saved `quot` rather more. v7 swept the i-list eight blocks at a time into
`struct dinode itab[256]` — **4,096 words of `bss`** here, where a `dinode` is sixteen words.
Condition 2 permits only whole blocks, and one block *is* `INOPB` dinodes, so the aligned buffer
**is** the i-node table and the array is gone.

### And `sync()` stopped being decorative

Both programs call `sync()` before reading, as v7's did. On the raw path that is load-bearing
rather than cautious: the read bypasses the buffer cache that the mounted filesystem is still
writing through, so without it `df` counts a free list the kernel has already moved on from.

## The unit they report in is not the unit they count in

They count **filesystem blocks**, 3072 bytes, and they print **1024-byte** ones — `KBPB` of them
per block, `KBPB` being 3 because 3072 is exactly three KiB.
[../README.md](../README.md) §4 is the rule and [../../include/sys/param.h](../../include/sys/param.h)
is where `KBYTE` and `KBPB` live, beside the `BSIZE` `KBPB` is derived from. What is worth
recording here is *why the multiply is at the `printf`* rather than where the size is divided,
because it looks like a stylistic choice and is not:

**`quot -c` decides it.** That histogram is `sizes[TSIZE]` **indexed by a file's block count**.
Convert at the source and every index triples: `TSIZE` covers a third of the file sizes it used
to, and two buckets in three can never be occupied, a size in KiB always being a multiple of 3.
Convert at the print and the index stays a block count, `TSIZE` is untouched, and the column that
needs the new unit — the first — gets it. Everything else follows for free: no accumulator
changes width, `du`'s recursion still returns blocks, `quot`'s sort key is still a block count so
the ordering cannot shift, and every variable named `blocks` still holds blocks.

Two things to expect of the output, and both are worth checking by eye after any change here:
**every number is a multiple of three**, the smallest allocation being one 3072-byte block; and
a number is **half** a PDP-11's, not a sixth, v7 having counted 512-byte blocks.

`ls -s` and its `total` line moved with them, so the same file cannot read 1 under one command
and 3 under another. `b6fsutil` deliberately did **not**: it is a filesystem inspector and the
rest of its report is on-disk block numbers, so `run-fsinfo.sh` converts once, in the one place
that already knows both sides.

## Two harnesses, and a third thing neither of them is

`df` and `quot` are the first programs in `cmd/` that can be tested under `b6sim` against **real
on-disk structure**. They read a *filesystem*, and a flat `b6fsutil` image is an ordinary host
file, so [test/fsimg.manifest](test/fsimg.manifest) builds one at build time — file sizes chosen
so that a ceil-divide by 3072 has something to get wrong, including a file of exactly one block —
and the cases walk its real free list and real i-list in a tenth of a second. **`mkfs` (C4c) and
`fsck` (C4d) should copy that rather than invent a second one.**

What those cases cannot say is anything at all about the four conditions above: `b6sim`'s
`read(2)` is the host's, so an unaligned buffer, a partial-block count and an off-block seek all
succeed there. That half is `kernel/test/fsinfo`'s, on volume 3087.

`du` is in neither world but the second: it reads directories with `read(2)`, and a host
directory descriptor refuses that — [../ls/README.md](../ls/README.md)'s limitation exactly.

**One thing cannot be asserted under `b6sim` at all, and it is not about this machine.** `quot`
maps a uid to a name with `getpwent(3)`, which opens the literal path `/etc/passwd` — so under
that harness it reads *the build machine's* password file. There is no uid this can be steered
around: `quot` ignores uids at or above `NUID` (300), and the whole range below that is system
accounts on both macOS and Linux. On the machine this was written on the fixture's uid 202 came
back as `_coreaudiod`; and uid 0 is unavoidable, the root directory having an owner, and is not
`root` either — macOS opens `/etc/passwd` with a `##` comment header, and `getpwent(3)` has no
notion of a comment, so the first line parses as an entry named `##` with an empty uid field.

## The oracles, and a rule for the ones that come after

`kernel/test/fsinfo` has four ([run-fsinfo.sh](../../kernel/test/run-fsinfo.sh)), and the shape of
them is the reusable half of this task.

**A number about the whole image must be recomputed, not remembered.** `run-files.sh` diffs
`b6fsutil -v -v` against a checked-in `files.modes`, which works because the modes it names are
the ones the test set. `df`'s and `quot`'s numbers are not like that: they move whenever anything
is added to `/bin`, so a checked-in table would be a file somebody has to update by hand for a
reason unconnected to the test. So the host works them out again — `b6fsutil -c -v`'s
`N blocks in use, M free` for `df` (times `KBPB`, that count being in filesystem blocks),
an `awk` over `b6fsutil -v -v` for `quot`'s per-uid sums —
and nothing needs touching as the image grows. `du`'s numbers *are* about a tree the test built,
so those are checked in, and nothing in the log is masked.

**A program that measures a filesystem cannot write its answer into it.** `quot >/tmp/x` counts
`/tmp/x` at the size it has *before* `quot`'s own output reaches it, and `df >/tmp/x` samples the
free list before the block that output needs is allocated — so either report, written to disk,
disagrees with the disk by exactly its own size and the host oracle needs a fudge factor nobody
can check. Both write to the **console** instead, between markers, and `run-fsinfo.sh` captures
this run's transcript. No test here had captured the console before.

Two things bit while writing those oracles, and both are the kind that pass silently:

* **The console expands tabs.** `quot` separates its columns with `\t`, and the terminal runs
  with `XTABS` ([../../kernel/dev/tty.c](../../kernel/dev/tty.c)), so by the time the report
  reaches the host they are spaces. A transcript cannot be split on a tab the guest emitted.
* **`b6fsutil -v -v` is two reports**, the superblock summary and then the tree. An `awk` that
  does not skip the preamble parses `Magic: 0123456701234` as an object — no third field, so a
  uid-0 entry of size 0 with an empty i-number — and the host comes out **one file ahead of the
  guest**. That is exactly how the first run failed, and `quot` was right.

## Three upstream bugs, fixed rather than carried

None is about this machine, and the second is the one that would have mattered.

* **`du` scanned its hard-link table one slot past what it had filled** (`i <= linked`). It read
  a zeroed `bss` entry, so it was harmless only because no real i-number is 0.
* **`quot`'s `qcmp()` called `strcmp()` on two `NULL`s.** `qsort` is handed all `NUID` entries and
  almost every one has `name == NULL`, for a uid `/etc/passwd` does not mention. On a PDP-11
  address 0 was readable and the comparison quietly returned something; here it dereferences word
  0. Both sides are guarded, and a named user sorts ahead of a bare number so the order stays
  total.
* **`quot` wrote one element past `du[NUID]`**: the `/etc/passwd` scan tested `n > NUID` where the
  array has `NUID` elements.

And one that is only a bug here: **`du`'s path append had no bound of any kind**, and it is the
worst of the unbounded buffers any port in this directory has met, because it *accumulates* — one
component per recursion level into a fixed `path[256]`. So does the backward scan in its
`chdir("..")` recovery, which walked off the front of the buffer when there was no `/` left to
find. [../README.md](../README.md) §6's "every port so far has had to bound one" holds again.

## Looked at and left alone, with the reason in the source

* **`quot` does not clear its accumulators between filesystems**, so `quot a b` reports running
  totals. v7's; there is one filesystem on this machine, so the case cannot arise and a
  divergence invented for it could not be tested.
* **`quot`'s `-n` mode** stays, and wants an `ncheck(1)` that is task C4e. The manual page says so
  rather than the code pretending otherwise.
* **`du.1`'s two BUGS are still true** — a non-directory argument prints nothing without `-a`, and
  more than `ML` distinct linked files are counted more than once. Both are asserted in
  `fsinfo.expected` rather than merely documented.
* **Neither `df` nor `quot` is setuid, and neither may become one.** `/dev/rmd0` is mode 0600
  because that one node is the contents of every file on the volume; a setuid `df` would hand the
  filesystem to anyone who could think of an offset. They are root-only programs and their pages
  say so. §8 is the general rule.

## Sizes

Against the 28,672-word ceiling, with `cat` for scale — most of all three is stdio.

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `quot` | 102 | 5,208 | 227 | 4,375 | **9,912** |
| `du` | 83 | 3,041 | 198 | 3,132 | **6,454** |
| `df` | 80 | 2,633 | 177 | 2,570 | **5,460** |
| `cat` | 83 | 2,989 | 165 | 1,544 | **4,781** |

`quot`'s `bss` is `du[300]` (1,200 words), `sizes[500]`, the superblock (512) and the aligned
block buffer (1,024) — it would have been 4,096 words larger had the `itab[256]` survived.
`du`'s is `ml[1000]`, 2,000 words, which is `du.1`'s second BUG made concrete. `df`'s is the
block buffer and the superblock and almost nothing else.
