# b6fsutil — Unix v7 filesystem images for the BESM-6

Builds, inspects, checks and unpacks root filesystem images for the kernel in
[kernel/](../../kernel/). It is a host tool: it runs on the build machine and
writes images the BESM-6 kernel mounts.

## Quick start

```sh
b6fsutil -n -M manifest.txt root.img    # build an image from a manifest
b6fsutil -c root.img                    # check it
b6fsutil -v root.img                    # superblock, then the tree
b6fsutil -S root.img md2053.disk        # convert to the SIMH container
```

then in the simulator:

```
sim> attach md00 md2053.disk
```

## Two container formats, and why

The image `b6fsutil` works in is **flat**: block *n* at byte *n* × 3072, each
48-bit word six big-endian bytes — the same word encoding `b6as` and `b6ld` use,
and it comes from the same place, `fputw`/`fgetw` in [cmd/libaout](../libaout/).
Flat images are dumpable with `od`, comparable with `cmp`, and easy to test.

SIMH attaches something else entirely. Its disk file stores each word as **eight
little-endian bytes** with a two-bit tag above the 48 (so an empty data word is
`0x0002000000000000`, not zero), and it **interleaves eight service words per
zone** — a zone being 1024 data words, of which a filesystem block is half. One
EC-5052 drive is 1000 zones, so a SIMH image is 8,256,000 bytes against the flat
image's 6,144,000.

`-S` converts between them, in whichever direction the input calls for. The
volume number comes from the output filename — the rightmost run of digits, range
2048–4095 — exactly as SIMH's own `attach` derives it, so `root.img` →
`md2053.disk` needs no flag. `--volume=N` overrides.

`simh_test.cpp` compares `format()`'s output against a digest of a disk written by
the real simulator, so the container is pinned to SIMH's definition of it rather
than to this tool's reading of `besm6_disk.c`.

## The layout

Everything is one 48-bit word. See [include/sys/](../../include/sys/) for the
kernel's own declarations; `params.cpp` static_asserts this tool's copy of the
constants against `sys/param.h`, so a kernel that retunes `INOPB` or `DIRSIZ`
breaks this build rather than the images it writes.

| | |
|---|---|
| Block | 512 words = 3072 bytes |
| Superblock | exactly one block, at block 1 |
| Inode | 16 words, 32 to a block, i-list from block 2 |
| Directory entry | 4 words — one i-number, three of name (18 chars) |
| Addresses per inode | 8: six direct, one single indirect, one double |

Three things routinely catch people out, and each has a test named after it:

- **`di_size` is in bytes, and 3072 is not a power of two.** There is no
  `BSHIFT`/`BMASK` — `sys/param.h` deleted them and explains why. Byte offsets
  divide.
- **A directory's size is `nentries * 24`, never rounded to a block.** Rounding
  makes `namei()` walk up to 126 empty slots per lookup and breaks slot reuse.
- **There is no triple indirect** (`NLEVEL == 2`) and **no symbolic links**
  (`sys/stat.h` has no `S_IFLNK`); the manifest rejects `symlink` rather than
  inventing an encoding.

## The manifest

Line-oriented, and compatible with the RetroBSD original minus `symlink`:

```
default
owner 0
group 0
dirmode 0755
filemode 0644

dir /etc
file /etc/passwd
source build/etc/passwd     # where to read it from on the host
mode 0444

link /etc/passwd.bak
target /etc/passwd

cdev /dev/tty
major 5
minor 0
```

Directories are created before their contents and hard links resolved last, so
the order in the file does not matter. Missing intermediate directories are
created on demand. `b6fsutil` can also generate a manifest from a host tree.

## Options

| | |
|---|---|
| `-n` | create a filesystem |
| `-s N` | volume size in blocks (default 2000, one EC-5052 drive) |
| `-i N` | inodes to make room for (default one per two blocks) |
| `-M f` | populate from a manifest |
| `-c` | check for consistency (five passes; reports, does not repair) |
| `-x dir` | extract everything to a host directory |
| `-a path file` | add one file to an existing image |
| `-v` | verbose; twice also lists the tree |
| `-T N` | fixed timestamp, for reproducible output |
| `-S` | convert between the flat and SIMH containers |

## Source layout

| File | |
|---|---|
| `fsutil.h` | the format: constants, `Word`, `Block`, the two conversions |
| `params.cpp` | emits no code; static_asserts the constants against `sys/param.h` |
| `image.cpp` | the flat container — **the only file that deals in bytes** |
| `simh.cpp` | the SIMH container, and conversion |
| `superblock.cpp` | `struct filsys`, and `sbcheck()` transcribed |
| `alloc.cpp` | `alloc`/`free`/`ialloc`, transcribed from `kernel/alloc.c` |
| `inode.cpp` | the i-list, `bmap()`, byte-granular file I/O |
| `dir.cpp` | fixed four-word entries, written from `kernel/nami.c` |
| `create.cpp` | mkfs |
| `manifest.cpp` | the manifest parser and host-tree scanner |
| `command.cpp` | paths: namei, add, list, extract |
| `check.cpp` | fsck |

Two invariants worth preserving:

**Only `image.cpp` and `simh.cpp` know a word has bytes.** A `Block` is
`std::array<Word, 512>`, never `unsigned char[]`. The RetroBSD source this was
ported from handed raw `unsigned buf[]` to its block writer in five places, which
quietly made its free list and indirect blocks correct only on a little-endian
32-bit host. Here that does not compile.

**`test/kernel_model_test.cpp` is a second, independent reader**, transcribed from
`kernel/alloc.c`, `subr.c` and `nami.c` in the kernel's own shape. It shares
nothing with the engine but the constants. Every other test checks the tool
against itself; that one checks it against the code that has to mount the result.
Keep it dumb — if it starts sharing helpers with the engine it stops being worth
anything.

## Status

Verified against the real simulator: `format()` matches `attach -n` byte for byte,
a populated image survives a flat → SIMH → flat round trip unchanged, and SIMH
attaches the result at the correct geometry.

**Not yet verified: an actual kernel mount.** `kernel/TODO.md` records that
`sbcheck()` has never executed — booting with a root disk attached hangs before
`iinit()` for reasons that predate this tool. The kernel-model test exists
precisely so this work could be finished and checked without waiting on that.
