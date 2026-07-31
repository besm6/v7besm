# `mount` and `umount`: the first block through the buffer cache on a second drive

Task C4f, and the last of task C4. `/etc/mount` and `/etc/umount` are on the image and
[kernel/test/mount](../../kernel/test/mount.ini) (volumes 3093 root, **3094 scratch**,
**3095 blank**) holds them there. Four things this port settled are worth having outside the
source, and the first is the reason the task existed at all.

## 1. Everything before this went to the raw device

`df`, `du` and `quot` read `/dev/rmd0`; `dd` moved bulk data through it; `mkfs` wrote a
filesystem onto `/dev/rmd1`; `fsck` repaired one there; `clri` and `icheck -s` wrote i-nodes
and free lists onto it. Every one of those is `physio()` → `mdstrategy()` → `mdwrite()`,
which is the path C4a mapped and the rest of C4 inherited.

**A mounted filesystem is not that path.** It is `getblk()`, `bread()`, `bwrite()` and
`bdwrite()` — the buffer cache, `kernel/dev/bio.c` — and until this task **`bdevsw[0]` minor
1 had never carried a block**. Nor had `smount()` ever had a caller: it has been compiled
into every `unix` this port has built, and `iinit()` fills `mount[0]` with the root by hand
rather than going through it, so the whole of `kernel/sys3.c`'s mount half was dead code with
a syscall number. `mount[1]` had never been filled either.

It worked first time, on the first boot, which is worth writing down precisely because it
might not have: the two halves — "the cache can write a block" and "the driver can reach
minor 1" — were each independently proven, and composing them needed nothing new.

The two properties of the cache path that made it safe are worth naming, because the raw
path's five conditions ([../df/README.md](../df/README.md), [../mkfs/README.md](../mkfs/README.md))
look as though they should apply and do not:

* **A cache buffer is always `BSIZEW` words and always page-aligned** — `buffers[NBUF][BSIZE]`
  lives at a fixed physical address (`kernel/README.md`) — so `mdstrategy()`'s count and
  alignment tests cannot fire on one. The alignment gymnastics every other C4 program does
  are the raw path's, not the disk's.
* **`getblk()` keys the cache on `(b_dev, b_blkno)`** and hashes per *major* through
  `bdevsw[major].d_tab`, so blocks from md0 and md1 share `mdtab`'s list and are told apart
  by `b_dev` alone. Nothing had to change for a second minor.

## 2. A `char[32]` is not 32 bytes, and v7's `/etc/mtab` is arithmetic that does not survive

This is the port hazard of the task, and it is a new one: not
[../README.md](../README.md) §2's `char *` ordering but the same fat-pointer machine seen
from the *layout* side.

v7 keeps the mount table as a binary record and writes it by blitting the struct:

```c
struct mtab { char file[32]; char spec[32]; } mtab[16];
write(mf, (char *)mtab, (mp - mtab + 1) * 2 * NAMSIZ);
```

A `char[32]` occupies ⌈32/6⌉ = 6 words = **36** char-units here, and the next member starts
on a word boundary, so `sizeof(struct mtab)` is **72 and not 64**. v7's own expression writes
64-byte records out of 72-byte objects: every entry after the first is read back eight bytes
out of step, and `mount` with no argument prints rubbish. Nothing about the source looks
wrong, and no compiler diagnostic exists for it — `2 * NAMSIZ` is simply a different number
from `sizeof(struct mtab)` on this machine and the same number on a PDP-11.

The generalisation is the one worth keeping: **a byte count computed from a field width is
not a struct's size here**, and any v7 source that `read`s or `write`s a struct is making
that assumption. Grep for `sizeof` in the neighbourhood of an I/O call, and where the source
computes the length by hand rather than asking `sizeof`, that is the bug.

Three ways out, and the file format is a program's own business so all three were open: widen
the field to a whole number of words (`DIRSIZ` 18 is the precedent this system already
carries), drop the struct for a flat `char` array with index arithmetic, or make the file
text. **Text won twice over**: nothing computes an offset at all, and the file can be diffed
by the test that writes it. [mtab.c](mtab.c) is the one implementation, compiled into both
programs rather than copied into each — v7 has four copies of the layout across two files,
and `umount.c`'s is the one with the `char *` comparison in it.

It cost the two programs their basename stripping as well, which is where three of their five
`char *` comparisons lived. v7 stored `md1` rather than `/dev/md1` because 32 bytes was
tight; a line is as long as it needs to be, so the special file is recorded as it was given
and `umount` matches the string the user typed. [include/man/mtab.5](../../include/man/mtab.5)
is the specification and carries the arithmetic above.

## 3. This is the first program of task C4 that has no `b6sim` world at all

[../README.md](../README.md) §9's rule is to use both harnesses where a program can run in
both, and every earlier C4 program could: they read and write a *filesystem*, and a flat
`b6fsutil` image is an ordinary host file, so `cmd/df/test`'s idea carried the whole task —
a fixture image, a real free list, a tenth of a second per case, and for `mkfs` and `fsck`
the strongest oracles in the tree.

`mount` reads no filesystem. What it does is call `mount(2)` — and b6sim services every
system call on the **host**, so a case that got that far would be asking the build machine to
graft a filesystem onto itself. There is nothing to fix and nothing to work around.

What is left is the argument handling, and it is only assertable because of a deliberate
divergence: **`mount.c` settles every argument before it opens `/etc/mtab`**, where v7 reads
the table first. Without that reordering the four cases in [test/](test/) would read the
*build machine's* `/etc/mtab` — absent on a Mac, a list of that machine's own filesystems on
Linux, and in neither case anything a checked-in `.expected` could name. That is
[../df/README.md](../df/README.md)'s `getpwent(3)` hazard in another guise, and the general
form is: **when a program reads a fixed absolute path, ask whose path it is under b6sim.**

So `ctest -L cmd` says nothing whatever about `mount(2)`, and
[kernel/test/mount](../../kernel/test/mount.sh) is the only thing that holds these two
programs. C4e recorded the mirror image of this — four programs asserted under b6sim alone,
with the device untested — and [../TODO.md](../TODO.md) carried it as a named loose end. That
end is closed here too: section 6 of `mount.sh` runs `icheck`, `dcheck`, `ncheck` and `clri`
over the real device, on a pack that has just been mounted, written and unmounted.

## 4. The privilege story is the device node, and it is a different one from the rest of C4

Neither program is setuid, and the reason is not the one `mkfs`, `fsck`, `clri` and `icheck`
carry. Those write `/dev/rmd0`, which is mode 0600 because that one node is every file's
contents. **These two never touch a raw device at all.** What gates them is `/dev/md1`, also
0600 and root's, and `mount(2)` asks for no privilege of its own — v7 gates it on the mode of
the special file and this kernel kept that, so there is no `suser()` call anywhere in
`smount()`, `sumount()` or `getmdev()`.

That makes the block node the entire gate, and a setuid `mount` would remove it: anybody
could then graft a filesystem of their own making — setuid-root files and all — onto this
one. The general question [../README.md](../README.md) §8 poses is "what call actually needs
privilege"; here the answer is *none*, and that is exactly why the bit must not be there.

## 5. What the kernel turned out to answer, since none of it had been exercised

Written down because the manual page had to be corrected against it, and because nothing
before this task could have found out:

* **`sbcheck()` retires this program's oldest BUG.** v7's `mount.1m` says in so many words
  that "mounting file systems full of garbage will crash the system", because v7's `mount(2)`
  reads the superblock and believes it. This kernel checks the magic number, the geometry it
  was built for, the i-list extent and the free counts (`kernel/alloc.c`), prints what it
  disliked on the console and answers `EINVAL`. The test mounts a **blank pack** on a third
  drive to prove it — `attach -n` formats every zone and leaves the data zeros, which is
  precisely a readable device with no filesystem on it.
* **`EIO` is a separate arm**, and `/dev/swap` reaches it: a block device whose superblock
  cannot be read at all fails in `bread()` before `sbcheck()` ever runs.
* **`NMOUNT` is 2 and the root holds one slot**, so exactly one filesystem may be mounted.
  `EBUSY` covers three different things — no free slot, a device already mounted, and a mount
  point whose inode is held more than once.
* **The shell that types `umount` is a process.** `sumount()` scans the whole in-core inode
  table and refuses while any live inode belongs to the device, so `cd /mnt; /etc/umount
  /dev/md1` is `EBUSY` every time. It is the cheapest thing in the test and the most
  convincing.
* **`s_ronly` reaches `access()`** (`kernel/fio.c`), so a create on a read-only mount is
  `EROFS`; and `iupdat()` silently skips the access time, which is what `mount.1m` warns about
  from the other side.

## 6. Two things the test had to be built around

* **The pack must be unmounted before anything raw touches it.** A raw read bypasses the
  buffer cache, so a measurement taken through `/dev/rmd1` while `/dev/md1` is mounted is a
  measurement of whatever the cache has not written back yet. Section 6 of `mount.sh` runs
  after the last `umount` for that reason and no other.
* **A recomputed oracle has to be recomputed at the right instant.** `run-mount.sh` holds the
  guest's free count against the host's walk of the image that came back — but the reading
  taken *through the mount* is deliberately not the count the pack ends with, `clri` having
  thrown a file away and `fsck` having reclaimed its blocks in between. The mounted reading
  is asserted as a literal by the diff instead, and the recomputed comparison uses a final
  `df /dev/rmd1`. **Ask what instant an oracle is a measurement of**, which is the sharper
  form of [../df/README.md](../df/README.md)'s rule about which kind of number it holds.

## Size

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `mount` | 93 | 3,428 | 374 | 1,079 | **4,974** |
| `umount` | 93 | 3,389 | 364 | 1,079 | **4,925** |

Of the 28,672 words a program has, and about 1,030 words of each `bss` is stdio's. They are
the two smallest programs of task C4 by a wide margin — `clri`, the smallest before them,
is 5,901 — because neither carries a block buffer: nothing here reads a device, and the
kernel does all the filesystem work these two ask for.
