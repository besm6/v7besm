# b6ranlib — the `__.SYMDEF` index for BESM-6 archives

Builds the table of contents that lets [`b6ld`](../ld) resolve a library in **one scan**:
`ranlib [-td] archive ...`. For every external symbol defined in a member it records the name
and the offset of the member that defines it, writes the lot as a member called `__.SYMDEF`,
and makes that the archive's **first** member. `-t` touches the index's timestamp without
rebuilding it; `-d` prints the table as it goes.

The entry format is [`cross/besm6/ranlib.h`](../../cross/besm6/ranlib.h), the archive around it
[`ar.h`](../../cross/besm6/ar.h), both through [`cmd/libaout`](../libaout).
[doc/Archiver_Manual.md §7](../../doc/Archiver_Manual.md) is the reference for how the index and
the linker fit together.

Build with `make`; the engine is covered by the GoogleTest suite in [`test/`](test).

## Source layout

| File | Responsibility |
| --- | --- |
| `ranlib.c` | The whole engine: the member walk (`nextel`), the symbol filter, `stash()`, `fixsize()` — which recomputes every offset by the delta the new index member introduces — `putrantab()` and `fixdate()`. |
| `main.c` | The thin wrapper: hands `argv` to `ranlib_run()` and forwards its exit code. |
| `symdef.h` | `ranlib_run()`, the one entry point. Named after the table rather than `ranlib.h`, which would clash with the cross header. |

**It does not write the archive itself.** Having built the index into a scratch file called
`__.SYMDEF` *in the current directory*, it runs [`../ar`](../ar) **in process** —
`ar rlb <first-member> <archive> __.SYMDEF` to insert a new index, `ar rl <archive> __.SYMDEF`
to replace one — through `ar_run()`, and then patches the index member's date so it is newer
than the archive. So a writable working directory is what this program needs, where `ar` needs
`/tmp`.

`ranlib_run()` returns its exit code rather than calling `exit()`, and since C9d that is
actually true: the four `exit(1)`s it used to have are a `jmp_buf` and a `fail()` now, cmd/ar's
`finish()` in miniature, closing the streams and removing the scratch `__.SYMDEF` on the way
out. `Ranlib.UnknownFlag` in [`test/`](test) is the case that could not be written before.

## Building for the BESM-6

These same sources — plus **all five of [`../ar`](../ar)'s engine files** and the `cmd/libaout`
files both call — are built a **second** time, by the `b6*` cross toolchain, into
`build/rootfs/usr/bin/ranlib`: task **C9d** in [../TODO.md](../TODO.md), with [`../ar`](../ar).
[`rootfs/CMakeLists.txt`](rootfs) is the whole of the build machinery, and there is **no second
copy of any source**.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `TABSZ` (`__.SYMDEF` entries) | 1000 | 512 | it is what `b6ld` will read |

### Two copies of the archiver, and why

`ranlib_run()` calls `ar_run()`, so the native `ranlib` carries the whole of `cmd/ar` inside it
exactly as the host `b6ranlib` links the `archiver` library. There are therefore two copies of
the archiver on the disk image. The alternative was to `exec("/usr/bin/ar")` on the target only,
and it was refused: the pair costs **37 of the 830 blocks the image had free** and sharing would
give back at most 16 of them, against a hard-coded
absolute path this program does not have today, a `fork` per archive where the host build does
the work in process, and an `#if besm6` fork *in the sources* — which is the one thing every
task from C9a onwards was arranged to avoid. The measurement is the argument: `ar` is 8,504
words and `ranlib` 11,333, both against a 28,672-word ceiling.

### `TABSZ`, measured and then chosen for a different reason

A `struct ranlib` is three words here, so the host's 1000 entries are 3,000 words of bss — over
a tenth of the address space, but affordable in a program that lands at 11,333. **The number
that decided it was not the address space.** [`../ld/intern.h`](../ld/intern.h) already caps
`b6ld`'s own `rantab[]` at `RANTABSZ` 512 on this target, so an index longer than 512 entries is
one **the machine's own linker refuses to read** — writing one would be a trap with nothing to
catch it. 512 it is, on both sides of the pipe.

The corpus agrees comfortably. Over every archive this repo builds the peak is **331** entries,
in `build/kernel/libunix.a` (43 members); `libc.a` is 223 over 188 members and `libcurses.a`
154 over 39.
`stash()` fails loudly on overflow — `symbol table overflow`, and now through `fail()` rather
than `exit()` — so undersizing is a clean stop and never a corrupted index.

`STRTABSZ`, beside it, was **deleted**: declared in 1990, referenced by nothing since, and it
read like a size knob that did something.

### Descriptors and streams

[../TODO.md](../TODO.md) expected the stdio-buffer arithmetic that dominated [`../ld`](../ld),
where twelve open streams cost 6,144 words of heap at the default `BUFSIZ`. It does not arise.
`ranlib` holds **one** `FILE *` at a time — `fi` is closed before `fo` is opened — so with
stdout and stderr that is three buffers, about 1,536 words, and no `setvbuf()` is called for.
`ar`, which this program contains, opens none at all. [`../ar/README.md`](../ar/README.md) has
the correction in full and the descriptor leak that *was* real: `ar_run()` is called once per
archive on the command line, and until C9d each call leaked its descriptors.

### The measurements

The program is **11,333 words** (138 const, 7,505 text, 852 data, 2,838 bss) with its top
relocatable symbol at 11,341 — against ceilings of 28,672 and 32,767. The bss is where the
difference from `ar` shows: 1,536 words of it are `rantab[512]`.

#### The stack

No recursion anywhere. The deepest chain is **982 words** of the 4,096:

```
main → ranlib_run (170) → ar_run (143) → cmd_replace (32) → append_new_files (19)
     → write_member (48) → copy_member (53) → die_write_error (5) → fprintf (2)
     → vfprintf (6) → _doprnt (281) → cvt (179) → exponent (36) → b$padd (8)
```

970 of those are reachable by any format string either program uses; `cvt` is `_doprnt`'s
floating-point arm and neither prints a float. **The whole of `ar`'s chain stacks on top of
`ranlib`'s own frame**, which is the one stack number in C9d that is not simply `ar`'s: the
difference between 982 here and 812 there is exactly `ranlib_run`'s 170 words. Without the
printing, the deepest archive path is 821, and `getarhdr`'s 341-word header buffer is again the
largest single frame in it.

#### The heap

`rootfs_ranlib_size` cannot see it: one `n_name` per stashed symbol (223 of them for `libc.a`,
freed in `putrantab()`), one member name at a time from `getarhdr()`, and the stream buffers
above.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test), over [`../ar`](../ar)'s fixtures — this
program *contains* `cmd/ar`, so two suites indexing different members would be two answers to
one question:

- **`rootfs_ranlib_index`, `_rerun`, `_touch`, `_symdef`, `_multi`, `_libc`, `_chain`** — the
  host `b6ranlib` and the native `ranlib` over one archive, the results compared byte for byte.
  `_index` takes the insert-a-new-index branch and `_rerun` the replace-one-in-place branch;
  `_touch` is `-t` alone; `_multi` puts two archives on one command line, which is the
  assertion for `ar`'s descriptor fix; `_libc` is the real corpus, 223 symbols over 188 members.
- **`cmd_ranlib_noargs`, `_badflag`, `_notfound`** — ordinary `b6sim` cases with a checked-in
  `.expected`. `_notfound` pins a **status of 0**, which is surprising and is the reason to pin
  it: the per-archive loop reports and continues.

**One word of the result is not deterministic, and the harness masks it.** `fixdate()` writes
`time(NULL)` into the `__.SYMDEF` member header, so two runs a second apart differ there and
nowhere else — every other member's `ar_date` is copied through from the input, and the index
member's uid, gid and mode come from the same host `stat(2)`, which `b6sim` passes through
unchanged. The header is fixed-size: a 6-byte `ARMAG`, then 1 length byte + the 9 bytes of
`"__.SYMDEF"` + 2 bytes of word padding, so `ar_date` is archive **bytes 18…23** and the ranges
either side of it are compared separately.

```
$ od -A d -t x1 -N 24 build/lib/libc/libc.a
0000000  00 00 00 00 ff 65  09 5f 5f 2e 53 59 4d 44 45 46  00 00  <ar_date>
```

**`rootfs_ranlib_symdef` needs no mask at all**, which is why it is there: it extracts the
`__.SYMDEF` *member* and compares the body, and the body carries no timestamp. Same symbols,
same order, same `ran_off` values — and since that order is what makes `b6ld`'s single scan of
a library work, a difference there is a real failure and never a cosmetic one.

**`rootfs_ranlib_chain` is the closing claim of C9d**: the native `ar` builds an archive out of
the C library's own objects and the native `ranlib` indexes it, and the pair is byte for byte
what the host `b6ar` and `b6ranlib` produce from the same inputs. The machine builds its own
`libc.a`.

By hand:

```sh
cd build
mkdir /tmp/c9d && cd /tmp/c9d
env -i b6sim $OLDPWD/rootfs/usr/bin/ar cr mine.a $OLDPWD/lib/libc/*.o
env -i b6sim $OLDPWD/rootfs/usr/bin/ranlib mine.a     # writes __.SYMDEF here
$OLDPWD/cmd/ranlib/b6ranlib -d mine.a | wc -l         # 225 symbols -- crt0.o is in there too
```
