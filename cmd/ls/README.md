# ls — 4.2BSD `ls(1)` for the BESM-6

`/bin/ls`, compiled by the `b6*` toolchain and staged as **`build/rootfs/bin/ls`**. Like
[`cmd/init/`](../init/), [`cmd/sh/`](../sh/) and its neighbours, this is a `cmd/` subdirectory
that is **not a host tool** — nothing here runs on the build machine.

**The source is 4.2BSD's, taken from RetroBSD** (`src/cmd/ls/ls.c`), which is the same upstream
[`../TODO.md`](../TODO.md) C10 takes `yacc` and `lex` from: 4.2BSD's program with the ANSI pass
already done. It replaces the v7 `ls` this directory carried before, and what it buys is what v7
never had — **multi-column output on a terminal**, `-F`, `-R`, `-A`, `-1`, `-q`, and the password
and group files reached through `getpwuid(3)`/`getgrgid(3)` instead of parsed by hand.

**8,193 words of the 28,672**, against the v7 program's 7,807 — 386 more for all of that, and
the `bss` actually *fell*, 2,640 words to 1,351, because upstream's `BUFSIZ`-sized buffers are
bounded here for what they hold. It remains the command that pulls in most of libc.

`stat_flags.c`, upstream's second source, is **not ported**: it serves `-o` and there is no
`st_flags` on this system. [`CMakeLists.txt`](CMakeLists.txt) is still one `b6_prog()` call over
one source.

## Building

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/rootfs/bin/ls among everything else
make run        # runs its size check (ctest `rootfs_ls_size', label `rootfs') with the rest
```

## It is the first caller of `directory(3)`

The libc directory library — `opendir`, `readdir`, `closedir`, `rewinddir`, `telldir`, `seekdir`,
`dirfd` — **arrived with this port and exists because of it**
([`lib/libc/man/directory.3`](../../lib/libc/man/directory.3.umm),
[`include/dirent.h`](../../include/dirent.h)). v7 had no such thing, and the eleven programs in
`cmd/` that walk directories each grew their own reader and their own copy of the same four
mistakes. `ls` was going to be the twelfth.

What that removes from this file is worth naming, because each of them is a bug somebody has
already written at least once here:

* the `d_ino == 0` test for a slot `unlink(2)` emptied,
* the copy loop that terminates the name, `d_name` on the disk being `DIRSIZ` characters with no
  room for a NUL when a name fills the field,
* the `DIRENTSZ` arithmetic, and
* the `fread()` of a `struct direct`, which is what made the v7 `ls` untestable under `b6sim`.

The other eight readers are **not** converted, and [`../TODO.md`](../TODO.md) **C24** is that task
— along with the eight programs that must *not* be converted, which read `struct direct` out of a
block they fetched from `/dev/rmd*` themselves.

## What the kernel takes away

There are **no symbolic links**, so `lstat()`, `readlink()`, `S_IFLNK`, the `-> target` printing,
the `l` type and the `@` suffix are all gone and upstream's `statf` function pointer collapses to
a plain `stat()`. There are **no sockets**, so `S_IFSOCK`, the `s` type and the `=` suffix go with
them. There is **no `st_flags`**, so the `-o` column and `stat_flags.c` are not here.

**`-L` and `-o` are still parsed**, and set a flag nothing reads, so that a command line written
for a BSD `ls` runs rather than failing. `ls.1.umm` says so under `OPTIONS THAT DO NOTHING HERE`, and
says *why* in each case — `-L` asks for behaviour every `stat` already has, and `-o` asks for a
field `struct stat` has not got.

There is **no `getopt(3)`** either. `options()` is by hand and takes the clustered form and the
separate one alike — `ls -lt` and `ls -l -t` — stopping at the first non-option or at `--`.

There is **no `TIOCGWINSZ`** and no way to ask this kernel how wide a terminal is. `twidth` is 80,
or `$COLUMNS` when that names a positive number. The rest of upstream's probe is real and is kept:
`isatty(3)`, `TIOCGETP` and `XTABS` decide `usetabs` exactly as it intends, so `ls -C` pads with
tabs or with spaces according to what the terminal does with a tab.

## What the machine forces

### `ISARG` is `S_IFREG`, and it links either way

Upstream packs its own flag `0x8000` into `fmode` beside `st_mode & ~S_IFMT`. **`0x8000` is
`0100000` is `S_IFREG`.** It got away with it on a machine whose `fmode` was a 16-bit `short`,
where the complement cleared the whole `0170000` type field and nothing above it existed; here
`st_mode` is a 41-bit `int` and `~S_IFMT` keeps every bit from 16 upward, so the two would be the
same bit and **every regular file would sort as a command-line argument**. `fmode` is masked to
`07777` instead — everything the `m1`…`m9` tables examine is `≤ 04000` — and `ISARG` is `010000`,
the first bit above them. The collision cannot come back however wide `st_mode` becomes.

The v7 port hit this too and answered it the same way. It is the one line in the file that fails
silently.

### A block is 3072 bytes, and a reported one is 1024

There is no `st_blocks` to ask, so `fblks` is `nblock(size)` — `(size + BSIZE - 1) / BSIZE`, a
divide, 3072 not being a power of two and there being no `BSHIFT` to replace it with. **`ls -s`
and the `total` line print `KBPB` (three) of them per filesystem block**, so the numbers are in
1024-byte blocks, and the multiply is at the two `printf`s and nowhere else — the rule
[`../README.md`](../README.md) §4 states for all four programs that report a block count.
`_Static_assert(BSIZE % KBYTE == 0, …)` is the other half of §4's rule, and the v7 program was
missing it.

Two consequences to expect: every number is a multiple of three, and it is *half* a PDP-11's
rather than a sixth. One further one this port inherits: with no `st_blocks`, a sparse file is
charged for the blocks it does not have.

### `-q` must not eat Cyrillic

Upstream replaces any byte `< ' ' || >= 0177` with `?`, and **`-q` is on by default at a
terminal**. A `char` is unsigned here and the whole path is byte-transparent
([`../README.md`](../README.md) §11), so that test would turn every byte of a Cyrillic name into a
question mark — the worst shape §11 describes, applied to the one program whose whole job is to
show you a name. The test is `c < ' ' || c == 0177`: control characters and rubout, nothing else.

The consequence is that **a column is a byte wide**, so a multi-byte name is charged for its
bytes. `ls.1.umm` has a section saying so rather than leaving it to be discovered.

### Bounded where upstream was not

`BUFSIZ` is **3072** here, so upstream's `static char dfile[BUFSIZ]` and
`static char fmtres[BUFSIZ]` would be 1,024 words of bss between them, and `gstat`'s
`char buf[BUFSIZ]` 512 words of stack. The third goes with `readlink`; the other two are sized for
what they hold (`MAXPATH`, `FMTSIZE`), which is where the bss saving came from.

Two real overruns are fixed rather than carried:

* **`fmtentry()` appended the name into `fmtres` with no bound at all.** Everything before the
  name is of known width, so `FMTSIZE` is sized so that nothing on any path has to test it
  ([`../README.md`](../README.md) §6) — but the loop is written against the end of the buffer
  anyway, the name being the one thing it copies that it did not measure.
* **`fmtinum()` `sprintf`'d `"%6u "` into `static char inumbuf[8]`**, which any i-number of seven
  digits overruns. It is sixteen characters and `%d` now; `ino_t` is a signed `int` here, and
  unsigned arithmetic lowers to out-of-line calls (`b$uadd`, `b$ult`, …).

### Smaller things

* **The entry array grows** where v7's was a fixed `flist[1024]`. The doubling is upstream's; the
  `MAXFILES` ceiling is this port's, because an `afile` plus a `strdup`'d name plus two `malloc`
  headers is about sixteen words and a 28-page address space gives out well before an unbounded
  array would. `ls: too many files` is v7's message, kept because the graceful failure was.
* **The array is sorted by value**, not through a vector of pointers as the v7 program was: the
  element is a multiple of `NBPW` and `calloc` returns byte #0 of a word, so
  [`qsort.c`](../../lib/libc/gen/qsort.c) takes its word-at-a-time exchange path over it.
* **`*fp = azerofile` is a `memset`.** The array came from `calloc`, so all-bits-zero is already
  what every field starts at — `fname` included, which `formatd()`'s `free()` loop tests against
  `NULL`.
* **`setpassent(1)` is deleted**; there is no such call here. The uid and gid caches stay, at
  `NCACHE` 16 rather than upstream's 64 — two 64-entry caches are ~384 words of bss for a
  `/etc/passwd` holding a handful of users. **They store the name, not the pointer**, and that is
  not an optimisation: [`<pwd.h>`](../../include/pwd.h) and [`<grp.h>`](../../include/grp.h) both
  say every pointer in the returned struct aims into one shared line buffer that the next call
  overwrites, so a cache of pointers would be a cache of the last name read, repeated.
* **`%ld` is `%d`** — there is no `long` on this machine to mean. The length modifiers are parsed
  and ignored, so it would have worked; what would *not* is `%D`, which this libc's `doprnt`
  echoes verbatim **and consumes no argument for**, desynchronising every later conversion in the
  same format. There are none left.
* **Errors go to stdout, both of them.** `%s unreadable` is upstream's own `/* not stderr! */`,
  and `%s not found` is on stdout in v7 and in the program this replaces. stdout is block-buffered
  in the guest and stderr is not, so moving either would reorder the logs that
  `kernel/test/*.expected` diff. That is a constraint, not a preference.
* `S_ISDIR()` does not exist here; `major()`/`minor()` come from `<sys/param.h>`, not
  `<sys/types.h>`.

## Testing

**There is still no `cmd/ls/test/`, and now for a different reason than before.** The v7 program
could not be tested under `b6sim` because it read a directory with `fread()` and the host refuses
to read a directory descriptor. That is still true of `opendir(3)` — but the failure is now
*quieter*, which is worth knowing: on the host, `open(2)` and `fstat(2)` on a directory both
**succeed** and only `read(2)` refuses, so under the simulator `opendir()` returns a perfectly
good `DIR` and every directory reads as **empty**. A `b6sim` case would pass while proving
nothing. [`lib/test/dirt`](../../lib/test/dirt.c) is `IMAGEONLY` for exactly this reason and its
head comment is the long version.

What *is* asserted, and where:

* `rootfs_ls_size` — the 28,672-word and word-32,767 ceilings, ctest label `rootfs`.
* `kernel/test/multi` — `ls -l /dev/console /dev/tty1` at a terminal, two owners resolved through
  `/etc/passwd`. Its expectation moved with this port: the long listing spends `%-9.9s` on an
  owner where v7 spent `%-6.6s`, and `%3d,%4d` on a device where v7 wrote `%3d,%3d`.
* `kernel/test/session`, `files`, `filters`, `dd`, `fsinfo`, `mount`, `edit`, `fsck`, `mkfs`,
  `tar` — all redirect, so `Cflg` is 0 and the one-name-per-line output is what it always was.
* `kernel/test/console` — `ls -1 /bin`, and the `-1` is new. A bare `ls` there is at a terminal
  and would print in columns, making the expectation a function of the screen width, of the
  terminal's `XTABS` bit and of the longest name in `/bin`, none of which that stage is about.
  It matters more than it would elsewhere because **that test is DISABLED**
  (kernel task 35), so a wrong expectation would sit there
  unnoticed.

**Column output is therefore not asserted anywhere**, and that is a real gap rather than an
oversight to be discovered later. Closing it wants a `-C` stage in an enabled weekly script —
forced with `-C` so that it works with the output redirected to a file, where `usetabs` is 1 and
the layout is deterministic.
