# novi — a full-screen editor, and the first program here that is not a port

**`novi` is by Dave W Plummer**, who wrote it for a split-I/D PDP-11 running 2.11BSD —
[the announcement](https://x.com/davepl1968/status/2080867382105710812). What is in this
directory is his program carried to the BESM-6; everything below is what that cost.

Task C12. Every other program under [`cmd/`](../) began in the v7 source tree, and the work
was carrying it to a 48-bit word-addressed machine. `novi` came from outside: it was written
in strict C89 for a machine with 16-bit `int`s, and there is no v7 original to be faithful
to. That changes what the eleven-point recipe in [`../README.md`](../README.md) has
to say about it — §1's C11 pass found nothing (the sources were already ANSI), §2 came back
empty, §3 came back empty — and it moves all of the work into one place: **the four
primitives this kernel does not have, and the two things a machine whose text is UTF-8
does not forgive.**

The design is the reason it was worth porting at all. **The document is never in memory.**
Two unlinked `/tmp` files hold it with a gap at the cursor — the text before the cursor
forward in one, the text after it *reversed* in the other — so the cursor position is simply
the length of the first file, and editing is appending to and popping off the two ends.
There is no `malloc` in 1,565 lines and nothing to run out of but `/tmp`. On a machine whose
whole user address space is 32 pages, that is not a curiosity.

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `novi` | 133 | 6,046 | 511 | 1,683 | **8,373** |

1,024 words of the bss are `terminal.c`'s output buffer and `buffer.c`'s two transfer
buffers, and they are in bss *on purpose*: see "the block move" below.

## Four primitives, and not one of them needed a substitute

This is the reusable half of the port. Each of the four was answered by **deleting or
designing away** the thing that wanted it, and in three cases the result is better than what
it replaced.

**`ftruncate(2)` — deleted.** `buffer.c` used it to shorten a scratch file after popping its
last byte. Nothing reads the scratch files' *physical* length: `buf_get()` bounds every
access by `nleft`/`nright`, the copy routines are handed explicit lengths, and every write
lands at the logical end and grows. So the files simply never shrink, each bounded by the
high-water mark of its own half — at most twice the document between them, in a 6 MB `/tmp`.
The general shape: **a program that already tracks its own lengths does not need the
filesystem to track them too.**

There is exactly one place where dropping it is *not* free, and it is the one a careless
port would miss. The cut buffer (`^K`) is a third scratch file that an appending cut extends
with `lseek(SEEK_END)` — and with no truncation, `SEEK_END` finds the tail of some earlier,
longer cut still sitting there. `E.cutlen` in `novi.c` is the length tracked explicitly, and
`do_uncut()` reads exactly that many bytes instead of reading to end of file. A long cut
followed by a fresh short one followed by `^U` is the case that separates the two, and it
was run.

**`mkstemp(3)` — replaced by the tree's idiom.** `mktemp(3)` exists; the rest is
`cmd/ed/ed.c`'s: `mktemp`, `creat`, reopen `O_RDWR`, `unlink`. Two traps come with it. The
template must be a **writable array** — `mktemp` writes through its argument, and a string
literal is in the const segment. And `mktemp` only avoids names `access(2)` can see, while
these files are unlinked the moment they are open, so **each of the three callers needs its
own template**: one template would hand back one name three times. Upstream got away with a
single `"/tmp/noviXXXXXX"` for both halves of the gap only through the order of its
statements.

**`fchmod(2)` and `rename(3)` — designed away together.** `buf_save()` used to `stat` the
file, `mkstemp` a sibling, `fchmod` it to the original mode, stream both halves into it and
`rename` it over the original. On this system `rename` is `link()+unlink()`
([`lib/libc/stdio/rename.c`](../../lib/libc/stdio/rename.c)) and `link` refuses a
destination that already exists — which is every save but the first. So the whole dance
went, and `buf_save()` is now `creat(name, 0666)` and the two copies, which is `ed`'s `w`.

**That is not a concession, it is the better answer**, and the reason is worth carrying:
`creat(2)` on an existing file **truncates it in place and ignores the mode argument**, so
the inode, the owner, the permissions and every hard link to the file survive untouched.
Preserving those is precisely what the `stat`/`fchmod` pair existed to fake. It also removed
a 512-byte automatic, the `strrchr`/`memcpy`/`strcat` path surgery that built the sibling's
name, and a failure mode in a read-only directory holding a writable file. The one thing
lost is atomicity, and it is in `novi.1.umm`'s BUGS: a write that fails half way leaves the file
truncated. `ed` on this system has the same exposure, so `novi` is adopting a hazard the
tree already has rather than introducing one.

Two smaller items, both hard compile errors rather than surprises: `open()` here is strictly
two-argument (no `O_CREAT` — [`include/fcntl.h`](../../include/fcntl.h) is an essay on why),
and `<sys/file.h>` is this tree's *kernel* open-file table and defines no `L_SET`/`L_XTND`;
`SEEK_SET`/`SEEK_END` come from `<unistd.h>`. There is no `<sys/ioctl.h>` header at all and
no `TIOCGWINSZ`, so `term_size()` is 24×80 unless `$LINES`/`$COLUMNS` say otherwise, and
upstream's `#if defined(pdp11)` termios/sgtty conditional has one live branch and was
deleted rather than kept as a branch that can never be taken —
[`lib/libcurses/cr_tty.c`](../../lib/libcurses/cr_tty.c) makes the same deletion.

## §11 twice, and both on the input side

[`../README.md`](../README.md) §11 is about a program that gives a meaning of its own to a
byte above `0177`. Both of `novi`'s instances are in what it *reads*, which is a shape the
section did not have an example of.

**`terminal.c` read `0233` as an eight-bit CSI introducer.** `0233` is `0x9B`, and `0x9B` is
the **second byte of Cyrillic Л** (U+041B = `D0 9B`) — and of Ы, Ю, and a good deal of lower
case besides. With that arm in place, typing Л swallowed the *next* keystroke as a CSI
parameter. Nothing on this machine emits an eight-bit CSI: the Consul line is `raw8` and a
host terminal sends 7-bit `ESC [`. The arm is gone, and `test/test_terminal.c`'s
`Cyrillic Л` row asserts that the two bytes of one letter come back as two bytes.

**`prompt()` accepted only `key >= 32 && key < 127`**, so no Cyrillic could be typed into a
file name or a search pattern — on a machine whose `/etc/motd` opens in Cyrillic and whose
shell globs it. `edit_loop()` already admitted the whole byte; the two paths disagreed. Now
both say `< 256`.

**And a third thing the same fact implies, which is not §11 but is its arithmetic.** A
column and a byte are different things. `visual_col()`, `at_column()` and `draw_line()`
counted bytes, so the cursor landed one column too far right per continuation byte and the
horizontal scroll drifted. All three now treat a byte in `0200..0277` as belonging to the
character before it and occupying no column — `cont()` is the whole of the rule — and
`draw_line()` keeps a byte count and a column count separately, padding by the column
deficit. A Cyrillic row comes out 92 bytes and exactly 80 columns.

## The block move: the one place performance was a correctness-shaped problem

Upstream migrates the gap **one byte at a time**: `lseek`+`read`+`ftruncate` off one file,
`lseek`+`write` onto the other. Count what the editor actually asks for:

| keystroke | bytes migrated | syscalls, upstream |
|---|---|---|
| `↓` | rest of line plus target column, ~80–160 | 320–640 |
| `PgDn` (nineteen of those) | ~1,500 | **~6,000** |
| `^W` finding a match far away | up to the whole file | unbounded |

Six thousand traps for one keypress on a simulated machine is not a performance nit.
`migrate()` moves `BUF_MOVE` (1,024) bytes per iteration instead — one `lseek`+`read` off the
source's tail, one reversal, one `lseek`+`write` at the destination's tail — so the same
`PgDn` costs eight. `buf_left`/`buf_right` are `migrate(…, 1, …)` and still cost four, which
is unavoidable and fine.

**The reversal is the same in both directions, and getting it backwards is silent** — the
bytes at the cursor stay right and the damage is a block away, so no interactive smoke test
would find it. `migrate()`'s comment carries the derivation; `test/test_buffer.c` carries
the seven multi-block seeks in both directions, each followed by a full byte-by-byte verify,
and those cases were written before the function was.

The two transfer buffers are **static, not automatic**. `copy_reverse()` upstream already
put two 256-byte arrays in one frame; at 1,024 each that would be 342 words of the
4,096-word stack at `070000`, which is the one size ceiling nothing checks (§6). In bss,
`rootfs_novi_size` weighs them.

`terminal.c` gained a 1 KB output buffer for the same reason from the other end. Upstream
issues one `write(2)` per string and `refresh()` emits a cursor address *and* a line per row:
about fifty `write(2)` calls carrying two thousand characters, per keystroke, through a
console whose clist blocks are thirty bytes. One buffer makes that two or three, and
`term_key()` flushes before it blocks so the screen is always what `novi` last drew. On top
of that, `refresh()` repaints the body only when the document's version counter, `E.top` or
`E.hscroll` has moved: a cursor key inside the window now costs one cursor address and the
status line instead of a screenful.

## Testing: three quarters of it runs, and the rest is deferred out loud

`buffer.c` touches no terminal and `terminal.c`'s decoder needs only bytes on descriptor 0,
so both have a full `b6sim` half — and that is where the risk of this port lives.
`test/CMakeLists.txt` has the long form. Four cases:

- **`cmd_novibuft_buffer`** — the whole gap buffer against a 20,000-byte document: the load,
  seven multi-block seeks both ways with every byte verified after each, single steps, the
  edits, `buf_insert_block`, and **two saves, the second over an existing file**, which is
  what `creat(2)`-in-place is here for.
- **`cmd_novikeyt_keys`** — twenty-six escape sequences through `pipe(2)`+`dup2(2)`,
  including the Л row and a truncated CSI that must not spin.
- **`cmd_novi_notty`** and **`cmd_novi_longname`** — the editor itself, which under `b6sim`
  reaches `term_open()`'s failure (`gtty` answers `ENOTTY` on a non-tty) and stops. Still
  worth having: `buf_open()` runs *first*, so `notty` really does create the scratch files
  and load a Cyrillic fixture into the gap block-reversed before the diagnostic.

**There is no typed SIMH dialogue, and that is a decision rather than an omission.**
`kernel/test/edit` used to fail because `send after=20000 "ed\r"` came back with a stray byte
between the two characters — the guest stalled mid-echo, kernel task 35, fixed — and so did the
same reason. A `novi` dialogue would be strictly worse: its output is escape sequences with
embedded cursor coordinates on an alternate screen, not readable text, and its input is
multi-byte arrow keys. A third test born disabled asserts nothing and costs a volume number
and a `simh_boot` lock slot. **`novi`'s interactive half goes in with the re-enabling of
`console` and `edit`, not before**, and task 35 is where that is recorded.

**Two other things are left open, neither a task in [../README.md](../README.md).** `novi` writes
hard-coded ANSI and has **no `termcap`**, so a terminal that is not ANSI cannot use it — and
**the blocker is gone**: `b6_prog()` has the `LIBS` keyword
[../../lib/libcurses/README.md](../../lib/libcurses/README.md) predicted, and
[`more`](../more/README.md) is using it. What is left is `novi`'s own work — `terminal.c` writes its
escape sequences into a 1 KB buffer of its own and reads its keys with a hand-rolled matcher,
both of which the library would replace. And **the search is literal**: `ed`'s regex engine is next door and is 700 lines of it;
whether `novi` should carry a second copy or the two should share one is the question to settle
before starting, not during.

What was done instead, by hand and not checked in: `novi` was driven under `b6sim` on a host
pty, which is the only place the screen logic can be watched running at all (`b6sim`'s `stty`
is a no-op, so the pty has to be put in raw mode from outside). It painted a four-line file
with a Cyrillic row at exactly 80 columns, cut a line, took six bytes of typed Cyrillic, saved
over the existing file and left. That is a development tool, not a test — nothing about it is
reproducible enough to check in — but it is what says the port works rather than merely links.
