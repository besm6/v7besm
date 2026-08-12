# `mkfs` — what the first raw *write* cost

Task C4c. This file is the account of the three things the port taught that belong to
whatever writes a device next, rather than to `mkfs`. The program itself is documented in
its own header and in [mkfs.1m.umm](mkfs.1m.umm); [../README.md](../README.md) is the porting recipe
and [../df/README.md](../df/README.md) is the account of the four alignment conditions a raw
*read* obeys, which are also all four of a write's. **Read that one first** — nothing here
repeats it.

The short version of the port is that `mkfs.c` came out shorter than it went in. v7's is 618
lines of which perhaps 250 are the prototype language, and that language is not ported
(`mkfs.1m.umm` says why, four reasons); what is left is 200 lines that read much more like
[../fsutil/create.cpp](../fsutil/create.cpp), the host's mkfs, than like v7's. That is
deliberate and it is what the oracle is built on.

---

## 1. A raw write has a fifth condition, and it is about *where the buffer lives*

`../df/README.md` lists four conditions on a transfer through `/dev/rmd0`. All four apply
unchanged to a write — `physio()` (`kernel/dev/bio.c`) never tests `rw`, it only ORs it into
`b_flags` — and there is a fifth that only a write can break:

```c
    // Not into the text.  useracc() below deliberately makes no read/write
    // distinction -- a page is open to data or closed to it -- so without this a raw
    // read would happily scribble on shared text.
    if (base < u.u_tsize)
        goto bad;
```

The comment is written for a read, where the text is the *destination*. For a write the text
is the *source*, and the check refuses it just the same: **a raw write may never be sourced
from a string literal or a `const` array.**

It cannot fire today, and the reason is worth knowing rather than discovering. `b6_prog()`
links an ordinary program `FMAGIC`, and `getxfile()` (`kernel/sys1.c:265`) forces
`ux_tsize = 0` for that magic — an impure program is one writable region and has no separate
text — so `u.u_tsize` is 0 and `base < 0` is never true. It becomes live the day something
that writes a device is linked **pure**, as `/bin/sh` and `/mnt/test/puret` already are. The
symptom would be an `EFAULT` from a `write(2)` whose buffer looked perfectly well aligned.

`mkfs` is not exposed to it in any case — it has one buffer and that buffer is bss — but the
idiom that would be exposed is the obvious one: `write(fd, "\0\0\0...", BSIZE)`.

## 2. The sector header is per *controller*, so a two-drive machine mislabelled a pack

This was the first bug in this port that needed a second drive to exist at all. It is fixed;
the account stays because the fix is not obvious from the code alone.

`kernel/dev/md.c` writes the disk's sector header out of a **fixed physical buffer**, eight
words at `030 + 8*ctlr`, and it used to maintain exactly one field of it: word 0 of each
half-zone group, the sector's own address. Words 1–3 — the volume's magic mark and **volume
number**, the userid and the checksum — were left as the last *read* brought them in, on the
grounds that every boot reads the superblock long before it writes anything.

It does. But the buffer belongs to the controller, not to the drive, and `md00` and `md01`
are two drives of one — so every half-zone the guest wrote onto the scratch pack came out
carrying **the root pack's** number. Measured, on the container `kernel/test/mkfs` produced:

```
zone 0 track 0 (block 0, never written)   ...7c1a000   volume 3090 -- the scratch pack's own
zone 1 track 0 (block 2, written by dd)   ...7c1b000   volume 3089 -- the ROOT pack's
zone 1 track 1 (block 3, never written)   ...7c1a000   volume 3090
```

It was survivable only because of which words are read back. `b6fsutil`'s `from_simh()`
(`../fsutil/simh.cpp`) validates the magic mark, one constant on every pack, and each
half-zone's self-address, the field the driver did maintain — and it *reports* the volume
number from **zone 0 alone**, without validating it. Zone 0 track 0 is block 0, and while
block 0 was a boot block nothing ever wrote it.

**The superblock lives at block 0 now**, so `update()` writes it on every sync and that rule
could not survive. `md.c` keeps `mdvol[]` instead — one word per drive, the mark and volume
of the last half-zone read from that unit, stamped into every write to it. A drive nobody had
read was left exactly as before, which was a no-regression rather than a guarantee: `tar cf
/dev/rmd1` then reached it, and task 37 closed it — `mdopen()` reads block 0 once per drive,
so the label is the drive's own before the first write either way.

The three `volume` greps — `run-mkfs.sh` (3090), `run-fsck.sh` (3092), `run-mount.sh` (3094)
— now assert the driver is right rather than that block 0 is untouched.

## 3. Committing last is a substitute for a size check

`mkfs` has to refuse a size bigger than the drive, and the drive's size is `MDNBLK` — a
constant of `kernel/dev/md.c` that no header exports to a user program. Duplicating it is
exactly what `../README.md` forbids: an on-disk constant has one home and a program asserts
against it rather than restating it.

So there is no size check. `mkfs` **reads the last block before it writes the first**, and
`mdstrategy()` answers the question itself — it refuses `blkno + wcount/MDTRACK > MDNBLK`
with `EIO`. One exchange, and it catches three different failures with one diagnostic: a size
larger than the drive, a drive nobody attached, and (under `b6sim`, where the special is an
ordinary file) a fixture too short for the size asked.

That works only because the **superblock is written last**. Until block 0 lands, the volume
has no magic number and `sbcheck()` will not mount it, so a run that dies partway through the
i-list or the free list leaves something obviously unfinished rather than something that
looks plausible. Given that, "probe, then write" is as safe as "check, then write" and needs
no constant.

`create.cpp` orders itself the same way and says the same thing about it. The two programs
now agree on their two error messages word for word, which is a small thing that pays off the
first time somebody tests one against the other.

---

## The oracle, and why it is byte-exact

`b6fsutil -n` is the host's mkfs, `mkfs.c` is a transcription of it, and the two can be
compared **byte for byte** — not field by field. What makes that available is narrow and
worth stating so that it can be kept:

* both start from zeros. `attach -n` formats every data word of a SIMH container to zero and
  `Image::create()` writes a zero file, so every byte neither program wrote is zero in both.
* `mkfs` writes only the i-list, the free-list chain blocks, the root's directory block and
  the superblock. It does **not** zero the data area (`mkfs.1m.umm` lists that under BUGS), and
  it must not start doing so.
* the only difference is the clock, and it is four fields fed from one number. `s_time` is
  word 6 of block 0 — byte offset **36**, six bytes big-endian — so the test reads it back
  out of what the guest wrote and hands it to `b6fsutil -T`, and the root inode's three times
  follow.

It is asserted **twice**, in the two worlds, and the split is `df`'s:

* `cmd_mkfs_layout` ([test/run-mkfs-test.sh](test/run-mkfs-test.sh)) runs `mkfs` under
  `b6sim` over a blank host file, in a tenth of a second, with a diffable failure. This is
  where the layout is debugged. It says nothing whatever about the device: `b6sim`'s
  `write(2)` is the host's, so all five conditions above are invisible there.
* `kernel/test/mkfs` (volumes 3089 root, 3090 scratch) makes the same comparison over the
  real path — `physio()`, `mdstrategy()`, `mdwrite()`, a second minor number and a second
  pack — and adds the things only a kernel can show: a refused write, `df` reading a
  filesystem that did not exist when the machine booted, and §2 above.

The fixture in the `b6sim` world is a **blank of exactly N blocks and not a filesystem**,
which matters: were it already an image, a `mkfs` that wrote nothing at all would pass every
check.

## Size

`const 85, text 2956, data 222, bss 2571` — 5,834 words, of which 1,536 are the two buffers
(`rawbuf` at 1,024 and `sblock` at 512) and about 1,030 are stdio's. Well inside the
28,672-word ceiling; §6's table has it beside `df`.
