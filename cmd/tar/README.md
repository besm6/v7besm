# tar, and the struct layout this tree had wrong

`/bin/tar` is task C7 ([../TODO.md](../TODO.md)) and the whole of it: one 935-line v7 source,
and the first program on this image that can move a **tree**. Everything before it — `dd`,
`mkfs`, `fsck`, `mount` — moves blocks.

§1's C11 pass was the usual mechanical work (all 41 functions were K&R, five untyped
`register i;`, four library re-declarations of which `char *sprintf()` was a hard conflict
with this libc's `int sprintf()`) and is not repeated here. What the port taught is below.

## The first thing it did was measure, and all three statements of the rule were wrong

The tar header is a **byte layout**: name at 0, mode at 100, uid at 108, size at 124, mtime
at 136, chksum at 148, linkflag at 156, linkname at 157. Those offsets are the format's, not
the program's — an archive has to stay readable by every other tar — so before anything else
this port had to know whether a `struct` here can express them. **Three** places in this tree
said it could not:

* [../README.md](../README.md)'s C5e section: "**a `char` member of a struct takes a word of
  its own here**", from `sed`'s `struct reptr` measuring "1,131 words for 100 entries, so it
  is eleven".
* [../mount/README.md](../mount/README.md) §2: `sizeof{char f[32]; char s[32];}` is "**72 and
  not 64**", the next member starting on a word boundary.
* [../../doc/Besm6_Data_Representation.md](../../doc/Besm6_Data_Representation.md) §8's type
  table: `char` with an alignment of **1w**, and §9's "every type is aligned to its own size"
  beside it. **This is the one that mattered**, and the port missed it: it is the reference
  `CLAUDE.md` names as authoritative and tells you to read before touching anything
  ABI-related, and it is where a reviewer went looking after the commit.

All three are false, and the measurement takes a minute:

```
sizeof(struct mtab)   = 66     not 64, and not 72
sizeof(struct flags)  = 6      five char members, packed
sizeof(struct header) = 258    257 rounded up to a word
sizeof(union hblock)  = 516    <-- and this one is the finding
off mode = 100  uid = 108  gid = 116  size = 124  mtime = 136  chksum = 148
off linkflag = 156   linkname = 157      <-- a scalar char member, and it is ONE BYTE
```

**Char members pack six to a word inside a struct exactly as they do in an array.** Only the
struct's *overall size* rounds up, `aggregate_align` being 6. So `struct header` lands on v7's
offsets exactly and the archives interchange.

**All three were wrong in the same direction and for the same reason, and that reason is the
finding**: the word is real, it is just the **object's** and not the **member's**. A standalone
`char c;` does occupy a word — `backend/besm6/frame.c` rounds every allocation up to one — and
that is a fact about allocation which does not reach inside a struct. `get_alignment()` returns
**1** for a `char` and, for an array, the alignment of its element, so 1 again; the target's
`aggregate_align` of 6 seeds only the struct's *own* alignment and therefore reaches nothing but
the **trailing** pad.

The C5e claim had a second cause stacked on that one: **`b6nm` prints octal**. `ptrspace` runs
from `021645` to `022775`, which is 600 words for 100 entries — six each — and `22776 - 21645`
is 1,131 only if the digits are taken for decimal. A `struct reptr` is five pointers and five
`char`s: 35 bytes, rounded to 36, six words. All three documents are corrected; `doc/` §4 gained
a `char inside a struct` subsection with the measured examples, and its §8 table now marks the
three character rows as the only ones where the object's answer and the member's answer differ.

**A corollary, because the same review asked it**: `char linkflag;` and `char linkflag[1];` are
identical here — alignment 1, size 1, same offset, same addressing mode, the compiler stripping
the array before it decides either. Writing the second to force an alignment the first already
has buys nothing, and the source says so at the struct so that the next reader stops where this
one did.

**Had any of them been true this program would have been incompatible with every tar in the
world**, and nothing in the build would have said so. That is the reason a layout claim is
worth a minute of measurement before it is worth an hour of design — and the guard that keeps
it measured is exact rather than approximate:
`_Static_assert(sizeof(struct header) == btow(HDRBYTES) * NBPW)`, which fails the build on any
interior padding at all. It was proved sharp by putting an `int` in front of `linkflag` and
watching the build stop.

## The union was the finding, and it hid behind a default

512 is not a multiple of six, so `sizeof(union hblock)` is **516**. v7 wrote

```c
union hblock { char dummy[TBLOCK]; struct header dbuf; } dblock, tbuf[NBLOCK];
...
copy(buffer, &tbuf[recno++]);              /* record n at byte 516*n */
write(mt, tbuf, TBLOCK*nblock);            /* record n at byte 512*n */
```

An **array** of that union has a 516-byte stride while every transfer on it moves 512 bytes
per record. With v7's default of one record per read *and* per write, only record 0 is ever
touched, the program is accidentally self-consistent, and nothing shows. Any `b` above 1 and
every record after the first in a physical block is four bytes further out than the last.

This is [../mount/README.md](../mount/README.md) §2's own finding — *a byte count computed
from a field width is not a struct's size here* — arriving from the union side rather than the
member side, and it is worth stating as a rule of its own:

> **A single object of a rounded-up type is safe; an array of it is not.** The rounding is
> invisible until something strides across it. Grep for an array whose element type is a union
> or a struct and whose transfers are a *constant* rather than `sizeof`.

`dblock` stays a union — one object, all I/O from its base, never `sizeof`'d — and `tbuf` is a
flat `char` array indexed by hand. `run-tar-test.sh` section 2 is the assertion, and it is
**sharp**: putting the defect back (addressing `tbuf` at `sizeof(union hblock)`) makes the
first `cmp` of section 1 fail.

## Three things a raw device wanted, and `../TODO.md` said none of them were needed

C7's brief said of `tar cf /tmp/x` and `tar cf /dev/rmd0`: *"Both work today; nothing in the
program needs a device this kernel has not got."* The second breaks three of `physio()`'s four
conditions ([../df/README.md](../df/README.md)):

| condition | v7 | here |
|---|---|---|
| count a whole number of `BSIZE` | `512 * nblock`, default 1 | default 6 on a character special; a factor not a multiple of 6 is **refused with a diagnostic** |
| buffer's word address a multiple of `MDALIGN` | an ordinary `bss` array | stepped forward with `ptrword()`, as `df`, `dd`, `quot`, `icheck`, `mkfs` and `fsck` all step theirs |
| seek block-aligned — **the silent one** | `backtape()` seeks −512 | `r` on a character special is refused outright |

The fourth (buffer at byte 0 of a word) v7 met by accident and this port keeps.

The rule to carry: **a claim that something already works is worth the same minute of
measurement as a claim about a layout.** Both claims in this task's brief were wrong, in
opposite directions — one said a hazard existed where it did not, the other said a path worked
where it did not — and the second is the expensive kind.

## What `tar` found in the kernel

`tar cf /dev/rmd1` is the first program in this tree to **write a pack it never reads**, and
that turns out to matter. `kernel/dev/md.c` keeps `mdvol[]`, the volume mark a write stamps
into the sector header, because the service-word buffer is the *controller's* and the mark is
the *drive's* — but it filled it only from a completed **read**, treating zero as "not seen
yet" and leaving the header alone. Leaving it alone does not leave it blank: it leaves whatever
the last read of any drive on that controller put there. The scratch pack came back calling
itself **3099**, which was the root's number.

`kernel/test/mkfs`'s oracle 1 was written for exactly this class of defect and passed only
because `mkfs` probes the last block before writing the first. Fixed as task 37:
`mdopen()` reads block 0 once per drive, so a pack now carries its own number whether or not
anything read it. `kernel/test/mdtest`'s check 15 is what holds it there — this test and
`run-tar.sh`, which asserted the defect deliberately, are both gone.

## Five things fixed rather than carried

Each is a visible bug, not a defensive change.

* **`printf("%7D", st->st_size)`** — §3's trap, and the second hit in twenty-five sources
  after `grep -c`. `doprnt()` does not know that PDP-11 conversion, echoes it verbatim and
  consumes no argument, so the whole size column of `tar tv` printed the two characters `%D`.
* **`response()` spun forever at EOF.** `char c = getchar()` — `char` is unsigned here, so
  `EOF` became 255 and `while (getchar() != '\n')` never ended. `tar xw` with a closed input
  hung. §11's input side, and C5a's rule about grepping a candidate for a hang *before*
  designing its tests: this one would have turned a ctest into a hang.
* **A 100-byte name has no terminator**, and v7 handed the header field straight to `creat`,
  `link`, `unlink`, `utime`, `chown` and `strcmp` — six routines that read on into the mode
  field and past it. The read side works from terminated copies now. §5's rule, with `NAMSIZ`
  where a directory entry has `DIRSIZ`; it was found by the test that archives a name of
  exactly 100 characters, which extracted nothing at all.
* **`strcmp(".", dbuf.d_name)`** — the same shape one layer down: `d_name` is `DIRSIZ` bytes
  and is not terminated when a name fills it.
* **The tail of the last record held the previous file's bytes.** v7 wrote `iobuf` whole after
  a short read, so the padding of every file carried up to 511 bytes of the one before it —
  an information leak, and noise for any byte-exact oracle. It is zero-filled.

Two more, and both are v7 misfeatures rather than bugs: `flushtape()` wrote a full physical
block even with nothing in it, and the **`blocksize = N` message was a tape property read off
a file**. A tape read returns exactly one physical block; a file read returns whatever is
left. So what v7 announced was the archive's *length* in records, never its blocking, and
`tar tf` of any archive shorter than `NBLOCK` said so on stderr. Reading needs no blocking
factor at all, so the guess and the message are gone and a file is read `NBLOCK` records at a
time — twenty times fewer `read(2)`s than v7's default, which is C12's point about counting
syscalls from the source rather than after.

## What was deleted, and what that bought

* **`u`.** Its index went to `/tmp` and was sorted and de-duplicated by
  `system("sort ...; awk ...; mv ...")`. There is no `awk` on this image (it is C10), so the
  dedup step would have failed silently and left the key quietly wrong. Gone with it:
  `tfile`, `mktemp`, `system()`, `checkupdate()`, `lookup()`, `bsrch()`, `cmp()`, `low`,
  `high`, and `dorep()`'s four `strcat()`s **onto an uninitialised stack buffer** — dead on
  arrival because the next line overwrote it, but they ran first.
* **`getwdir()`**, which forked `/bin/pwd` down a pipe so that `dorep()` could `chdir()` back
  after each argument, and the `chdir()` recursion it served. `putfile()` takes a full path
  and opens it, which stores the same name in the same header. That deletes a fork, a pipe,
  an exec, a 50-byte read into a 60-byte buffer, an unbounded scan for a newline, and the
  re-`open(".")` plus `lseek` after every recursive call. **It is also what gives the program
  a `b6sim` half at all**: there the exec fails and the parent would `chdir` the *build
  machine* to `/`. C4f's finding — that a partial `b6sim` half can be *made* by reordering
  what v7 did — applies, except that here the reordering is worth having on its own merits.
* **`/dev/mt1` and the drive digits `0`–`7`**, which named nothing. Not repointed at a disk:
  `/dev/rmd0` is the pack the system runs on and a one-character typo that rewrites it is not
  a default worth having. `f` is mandatory.
* **`select()`** is `selbit()`, as `ls` renamed the identical routine and for the same reason.

## The frame was estimated at 30 words and measured 239

`putfile()` recurses, so its frame is paid once per level, and §6's third ceiling — 4,096
words of stack — is the one nothing checks. The port shrank v7's per-level `char buf[TBLOCK]`
to a `char child[NAMSIZ + 1]`, counted the locals, and estimated **thirty words**. `b6disasm`:

```
 1444:	15 utm 0357	76500357
```

`0357` is **239**. Eight times the guess, and the same lesson `find` cost in C5f at a factor
of two: **read the prologue.** Against 4,096, with `_doprnt`'s 281-word frame underneath the
diagnostic that reports the refusal, the stack allows 14 levels. The walk also holds one open
descriptor per level — v7 closed and re-opened each directory because it `chdir`'d away, and
this port does not — and `NOFILE` is 20 less the four already spent, so the **descriptors bind
first** and `MAXDEPTH` is theirs. A `_Static_assert` holds the two in that order, so that
changing either cannot silently make the stack the tighter one.

## Which harness says what

Eleven `cmd_tar_*` cases under `test/` and one booted test, and the split is unusually clean.

**`b6sim`** gets the header layout, the record arithmetic, the blocking factor, the bounds and
both directions of interchange, in under a second. `ref.tar` is a **checked-in v7-format
archive** made once by the host's `tar` over three files stamped 1979 and chmod'd 644, 600 and
755, so `cmd_tar_listv` diffs a verbose listing whose every column is a header field at a
fixed offset. The date column is safe in a checked-in file because
[../../lib/libc/gen/ctime.c](../../lib/libc/gen/ctime.c) takes its zone from `ftime(2)` and
`b6sim` answers zone 0 — it does not depend on the developer's `TZ`.

**The host's own `tar` is the second implementation**, C5e's third oracle, and here it
genuinely speaks the format. What it cannot be is a **byte-for-byte** oracle: v7 blank-pads
its octal fields where a modern tar zero-pads them (`"   644 "` against `"0000644"`), both are
legal and each reads the other, so what is compared is the **files**. C4c's rule wants the two
implementations to be transcriptions of *each other*; two transcriptions of the same *format*
are a weaker relation, and the distinction is worth having in writing.

**`kernel/test/tar`**, volume 3099 with a scratch pack at 3100, has four things that exist
nowhere else: the tree walk (`b6sim` refuses to read a directory descriptor — `ls`'s, `du`'s
and `find`'s limit), `checkdir()`'s exec of the setuid `/bin/mkdir`, the owner/mode/mtime an
extraction restores, and `/dev/rmd1`. Its sharpest oracle is that **the host's `tar` reads the
archive straight off the bare disk pack** — no filesystem on it at all — which nothing but a
correct alignment, blocking factor and write path could produce.

Two of its assertions are a **matched pair**, and the second was written because the first
passed for the wrong reason. `find -newer` against a marker the script makes two seconds after
its corpus must print **nothing** (the mtimes were restored) and the same extraction under `m`
must print **everything** (they were not). The `m` half printed nothing on the first run — the
guest clock advances about two seconds over a whole run, so the extraction landed in the same
second as the marker and *strictly* newer was false. It needs a `sleep` of its own. **A
negative that cannot distinguish is not a negative**, and an empty result is exactly what a
passing test looks like.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `tar` | 131 | 6,243 | 546 | 3,578 | **10,498** |

Against §6's 28,672 words, a bit over a third — between `fsck`'s 10,842 and `quot`'s 9,912,
and the largest program of any task since C4. 6,243 words of text is the most in `cmd/` after
`sed` and `sh`, and `sscanf` is why: `getdir()` parses five octal fields with it and no other
filter on this image pulls `doscan.c` in.

Of the 3,578 words of `bss`, **2,219 are `tbuf`** — 1,707 words for `NBLOCK * TBLOCK` and up
to 511 more of alignment pad, charged once so that a raw transfer can start on a half-zone
boundary. `dblock`, `iobuf` and `zblock` are 86 words each and stdio is about 1,030. The
20-record buffer is kept at full size even though a raw device can only use 6, 12 or 18 of it:
`NBLOCK` is the archive format's constant, not this machine's, and 512 words is a cheap price
for not having a second number to keep in step.

Nothing here is allocated: the one `malloc` is a 21-word `struct linkbuf` per multiply-linked
file, and it checks its result and degrades to a diagnostic. §6's fourth ceiling — the heap,
which `rootfs_tar_size` cannot see — is reachable only by an archive with a very large number
of hard links.
