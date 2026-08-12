# b6ar — archiver for BESM-6 `a.out`

Packs many files into one, most often the object files that make up a link library.
`ar [-]{mrxtdpq}[uvnbaicl] archive file ...`, v7's letters exactly — one command letter
(`r` replace, `d` delete, `x` extract, `t` list, `p` print, `m` move, `q` quick-append) and
any of the options (`u` only if newer, `v` verbose, `a`/`b`/`i` position, `c` no "creating"
notice, `l` temporaries in the current directory, `n` ignored).

The on-disk format is [`cross/besm6/ar.h`](../../cross/besm6/ar.h), read and written through
[`cmd/libaout`](../libaout). **[doc/Archiver_Manual.md](../../doc/Archiver_Manual.md) is the
manual** — the commands one by one, the header layout, the word-padding invariant, the
diagnostics and a worked example. What follows is the source layout and what building it for
the machine itself cost.

Build with `make`; the engine is covered by the GoogleTest suite in [`test/`](test), which
links it in-process.

## Source layout

| File | Responsibility |
| --- | --- |
| `ar.c` | `ar_run()`: the flag walk, the one command letter, the signal traps, and `struct arstate ar`, the single instance of all mutable state. |
| `command.c` | The seven command handlers, one function per letter. |
| `archive.c` | The streaming engine — `copy_member()`, the temp files, `next_member()`/`match_member()`, and `handle_position()` for `-a`/`-b`/`-i`. |
| `list.c` | The `t -v` long line: the nine-column permission string and `ctime()`. |
| `util.c` | `finish()` (the single exit point), the diagnostics, `basename_of()`. |
| `main.c` | The thin wrapper: hands `argv` to `ar_run()` and forwards its exit code. |
| `intern.h` | `struct arstate`, the `SKIP`/`IODD`/`OODD`/`HEAD` flags, every prototype. |
| `archive.h` | The public, C++-safe surface: `ar_run()` alone. That is what [`../ranlib`](../ranlib) links. |

**No command edits an archive in place** but `q`. The rest stream the old archive
member-by-member into a temporary, making the change as they go, and copy the temporary back
over the original — which is why there are up to three temp files at once and why `finish()`
is the only way out.

## Building for the BESM-6

These same sources — plus the `cmd/libaout` files `ar` calls — are built a **second** time, by
the `b6*` cross toolchain, into `build/rootfs/usr/bin/ar`: task **C9d** in
[../README.md](../README.md), with [`../ranlib`](../ranlib).
[`rootfs/CMakeLists.txt`](rootfs) is the whole of the build machinery, and there is **no second
copy of any source**.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| — | | | *nothing* |

**The table is empty and that is the finding.** Not one line of `cmd/ar` keys off the `besm6`
macro. It is the third of the nine programs built twice — after [`../size`](../size) and
[`../disasm`](../disasm) — that the address-space ceilings did not touch, and the first of the
three that is not trivial: 1,024 lines over six files, seven commands, three temp files.

The reason is `struct arstate` ([`intern.h`](intern.h)). It holds *all* the mutable state, which
is exactly the v7 shape that cost C9a and C9b so much — `struct cppstate` was ~38,600 words,
`struct assembler` ~28,300, `struct linker` ~50,400, and every one had to have its big arrays
lifted to file scope before `b6as` would take a member offset ([../README.md](../README.md) §6).
`struct arstate` is **~190 words**: an `iobuf[512]` (86 words), a `member_name[256]` (43), a
`struct stat` (11), a `jmp_buf` (8, `int[8]` here), three 20-byte templates and about thirty
scalars. It was never near the 4,096-word limit, because the arrays it holds are *buffers* and
not *tables* — nothing in `ar` is sized by the input.

### `mkstemp()`, which this libc had not got

`ar` calls it three times ([`archive.c`](archive.c) twice, [`command.c`](command.c) once), and
this was the fourth toolchain program to want a scratch file. The other three escaped:
[`../as`](../as), [`../ld`](../ld) and [`../strip`](../strip) all moved to `tmpfile()`, because
**only the stream was ever wanted there**. `ar` cannot follow them. It wants the *name* —
`finish()` unlinks all three — and it wants a descriptor it can `lseek()` back to zero and
*read*, because `commit_archive()` copies the temporaries into the rebuilt archive at the end.
`tmpfile()` gives a `FILE *` and no name, and this `ar` never opens a `FILE` at all.

So `mkstemp()` was added to libc: [`lib/libc/gen/mkstemp.c`](../../lib/libc/gen/mkstemp.c),
declared in [`<stdio.h>`](../../include/stdio.h) beside `mktemp()`, with
[`lib/libc/man/mkstemp.3`](../../lib/libc/man/mkstemp.3.umm) and cases in
[`lib/test/gen.c`](../../lib/test/gen.c). That keeps these sources character-identical in both
builds, which no `#ifdef` in `ar` could have done.

**Two things about it are this machine's, and both are in the man page's BUGS section.**
`mktemp()` *consumes* the run of `X` on its first call, so a `mkstemp()` written as a retry
loop around it would offer the same name for ever — the loop is written out instead. And there
is **no `O_CREAT` and no `O_EXCL`** in this kernel ([`<fcntl.h>`](../../include/fcntl.h) says
so at length): `creat()` is the only way to make a file and it hands back a *write-only*
descriptor, so `mkstemp()` reaches read-write through a `creat()` and a reopen — the same dance
[`lib/libc/stdio/endopen.c`](../../lib/libc/stdio/endopen.c) does for `"w"`. Which means this
`mkstemp()` **inherits `mktemp()`'s race rather than closing it**. What it buys the caller is
that the file exists, and is open read-write, before a byte is written to it. On a machine with
one operator that is the whole of what was wanted.

Under `-l` the templates become bare names in the current directory rather than `/tmp/ar[012]…`,
so `ar` is the second program here after `strip` that wants the writable `/tmp`
[../../scripts/root.manifest](../../scripts/root.manifest) provides — and the first that can be told not to.

### Descriptors, and a correction to the TODO

[../README.md](../README.md) framed `_NFILE` as this program's stdio-buffer problem, the way
`ld`'s twelve open streams were. **That is not what is true here.** `ar` opens *no* `FILE`:
every archive, temporary and member file is a raw descriptor, and the only stdio in the program
is `printf`/`fprintf` on the two streams `crt0` provides. What binds is the *descriptor* count,
and the peak is **five** — archive, main temp, before-temp, move-temp, one input — against
`NOFILE` 20 ([`<sys/param.h>`](../../include/sys/param.h)).

That count did have a bug, and C9d fixed it: `finish()` unlinked the temp files but **closed
nothing**, which costs nothing in a one-shot `b6ar` process and breaks `ranlib`, which calls
`ar_run()` once per archive on its command line. Four leaked descriptors a call would have
stopped `ranlib *.a` after the fifth archive on a machine that has twenty.
`reset_state()` now seeds the five to `-1` rather than the `memset`'s 0 — which is *stdin* —
and `finish()` closes each above 2 on the way out. `rootfs_ranlib_multi` is the assertion.

### The four `libaout` file-descriptor routines, natively for the first time

`getarhdr`, `getint`, `putarhdr` and `putint` are exactly the difference between
`B6_LIBAOUT_SOURCES` and `B6_LIBAOUT_SOURCES_NATIVE`
([`sources.cmake`](../libaout/sources.cmake)). They exist for `ar` and `ranlib` and for nothing
else — the seven earlier native tools read and write through the `FILE *` flavour — so C9b and
C9c never cross-compiled them.

One line in them had no precedent anywhere in the `b6cc`-compiled tree:
[`putint.c`](../libaout/putint.c)'s `unsigned char b[6] = { i >> 40, i >> 32, … }`, an
**automatic** aggregate initialised from runtime values. If it had miscompiled the failure
would have been silent — a wrong `ARMAG` at the head of every archive `ar` creates. It does
not; the six shifts land where they should, and `rootfs_ar_create` is the standing check.
[`../ranlib/ranlib.c`](../ranlib/ranlib.c)'s `char *av[] = { "ar", "rlb", … }` is the same
class of construct (an array, not the forbidden *struct* initializer) and is likewise correct.

### Signals

`ar_run()` traps `SIGHUP`, `SIGINT` and `SIGQUIT` so a half-written temporary is removed on the
way out, and `commit_archive()` and `cmd_quick()` ignore all three while the real archive is
being rewritten. **Both halves work in both worlds**: this kernel delivers signals
([`kernel/sendsig.c`](../../kernel/sendsig.c)) and so does `b6sim`, which remembers the guest's
disposition and runs the handler at the next syscall return.
[`<signal.h>`](../../include/signal.h)'s remark that b6sim answers anything but
`SIG_DFL`/`SIG_IGN` with `EINVAL` is stale and was corrected with this task.

### The measurements

The program is **8,504 words** (134 const, 6,403 text, 741 data, 1,226 bss) with its top
relocatable symbol at 8,512 — against ceilings of 28,672 and 32,767.

#### The stack

No recursion, and no function holds an array; the deepest chain is **812 words** of the 4,096:

```
main → ar_run (143) → cmd_replace (32) → append_new_files (19) → write_member (48)
     → copy_member (53) → die_write_error (5) → fprintf (2) → vfprintf (6)
     → _doprnt (281) → cvt (179) → exponent (36) → b$padd (8)
```

800 of those are reachable by any format string this program actually uses — `cvt` is
`_doprnt`'s floating-point arm and nothing in `ar` prints a float. Strip the printing out
altogether and the deepest *archive* path is **651**:

```
main → ar_run (143) → cmd_replace (32) → next_member (43) → getarhdr (341)
     → malloc (62) → ialloc (30)
```

`getarhdr`'s **341 words** is the largest single frame in the program and it is not `ar`'s: it
is `libaout`'s `unsigned char b[ARMAXNAME + W + 5*W]`, the 291-byte scratch a member header is
decoded through. `_doprnt`'s 281 is the other half of [../README.md](../README.md) §6's rule
about what a program prints with.

#### The heap

`rootfs_ar_size` cannot see a byte of it, and there is very little to see: one member name at a
time, `malloc`'d by `getarhdr()` and freed by `next_member()` before the next, plus stdout's
one `BUFSIZ` buffer. Nothing in `ar` grows with the archive — the 512-byte `iobuf` is the whole
of its working storage, and a 177-kilobyte `libc.a` goes through it in chunks like any other.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_ar_create`, `_replace`, `_update`, `_delete`, `_before`, `_move`, `_quick`,
  `_local`, `_toc`, `_tocv`, `_print`, `_extract`, `_libc`** — the host `b6ar` and the native
  `ar` over one set of inputs, the two archives compared **byte for byte**. One case per
  command letter, because the GoogleTest suite beside this file reaches only `r`, `x` and `d`:
  `t`, `p`, `m`, `q` and the `-a`/`-b`/`-i` positioning had never been executed by anything,
  which is how `list.c`'s use of `S_IRUSR`…`S_IXOTH` — nine names
  [`<sys/stat.h>`](../../include/sys/stat.h) did not define until this task — went unnoticed.
  `_before` and `_move` are there for the second and third `mkstemp()` call sites specifically.
- **`cmd_ar_noargs`, `_badflag`, `_nocommand`, `_twocmd`, `_notfound`, `_qab`** — ordinary
  `b6sim` cases with a checked-in `.expected`, which is the other half: they pin the
  diagnostics and the exit status, which two `ar`s wrong in the same way would not.

Three rules the harness keeps, each for a reason worth knowing:

- **The inputs are copied in with `cp -p`.** A member's `ar_date` is the input file's
  `st_mtime`, so the two sides must see the very same timestamps or every header would differ.
- **The host side runs under `TZ=UTC0`.** `ar tv` renders `ar_date` with `ctime(3)`, and under
  `b6sim` this libc is told zone 0, so the native side prints UTC while an unconstrained host
  side would print the build machine's local time. `cmd/pr/test/run-pr-test.sh` carries the
  same line.
- **Each side gets a directory of its own.** `ar x` writes member files into the current
  directory and `-l` writes temporaries there.

`rootfs_ar_libc` is the one that matters: it archives the real `build/lib/libc/*.o`, 189
objects and 177 kilobytes, and takes about a fifth of a second. `rootfs_ar_size` (registered by
`b6_prog()`) is the third kind, holding the program under the two address-space ceilings.

The check by hand is the machine archiving the library it was linked against:

```sh
cd build
env -i b6sim rootfs/usr/bin/ar t lib/libc/libc.a | wc -l    # 189: 188 members + __.SYMDEF
env -i b6sim rootfs/usr/bin/ar cr /tmp/x.a lib/libc/*.o
env -i b6sim rootfs/usr/bin/ar tv /tmp/x.a | head           # list.c, in UTC
```

**No `kernel/test/` volume of its own.** C9a–C9c added none either: `ar`'s system calls are
ordinary file I/O that `b6sim` services faithfully, and the one kernel-side thing — a temporary
in the image's own `/tmp` — [`../strip`](../strip) already exercises. That was a decision, not
an omission.
