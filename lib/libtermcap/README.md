# libtermcap — the terminal capability library for the BESM-6

`libtermcap.a`: `tgetent`, `tgetnum`, `tgetflag`, `tgetstr`, `tgoto` and `tputs`, reading the
`/etc/termcap` that [`etc/`](../../etc/) stages onto the root filesystem. The 4.xBSD/2.11BSD
termlib, ported from the reference copy in `lib/tmp/libtermlib/`; [`../README.md`](../README.md)
listed it as not written yet, and this is it.

It exists to be linked against, and its first consumer is now
[`../libcurses/`](../libcurses/) — 4.3BSD curses, which calls every routine here.
[`../../include/curses.h`](../../include/curses.h) and
[`../../include/unctrl.h`](../../include/unctrl.h) had been in the tree all along waiting for
that, though `curses.h` had to be replaced when it came: what stood there was v7's, and the
sources are Berkeley's later ones.

## Building and linking

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/lib/libtermcap/libtermcap.a among everything else
make install    # puts it in share/besm6/lib beside libc.a, libm.a and libruntime.a
make run        # runs the termcapt test twice -- see Testing
```

**Link order is a contract, not a style:** `-ltermcap` comes **before** `-lc`.

```sh
b6ld crt0.o prog.o -ltermcap -lc -lruntime -o prog
```

`b6ld` scans each archive exactly once, in order. termcap calls `getenv`, `open`, `read`,
`close`, `write`, `strlen`, `strcpy` and `strncpy` in libc, and libc calls nothing back — the
same one-scan reasoning [`../README.md`](../README.md) gives for `-lm`. In the CMake build,
`b6_libtest(<name> libtermcap)` does it for a libc test program and
`b6_prog(<name> ... LIBS libtermcap)` for a program bound for the image —
[`cmd/more`](../../cmd/more/README.md), the pager, being the first and so far only caller of
the second.

The whole library is **1,075 words** — 761 for `termcap.o`, 206 for `tgoto.o`, 108 for
`tputs.o` — and one function per file means `b6ranlib`'s index lets a program that only calls
`tgetent` pay for only that.

`tcattr.c` of the upstream directory is **not** here. It is a `tcgetattr`/`tcsetattr` shim
wrapped entirely in `#if 0`, and this system has `sgtty`, not `termios`.

## The port: four `char *` comparisons

This is the whole of the interesting work, and it is all in
[`termcap.c`](termcap.c). [`../../cmd/ls/README.md`](../../cmd/ls/README.md)'s fourth fat-pointer
hazard is the one that bit here, over and over: when this port was written a relational between
two `char *` compiled to a whole-word integer comparison, the byte offset in bits 47–45
dominated the word address in bits 15–1, and — because the offset *decrements* as the pointer
advances — the ordering came out scrambled and inverted within a word.

**The compiler has since fixed it** (2026-06-17): such a relational now lowers through
`b$pdiff` and tests the sign, so none of what follows was strictly necessary and none of it is
wrong either — an index test is a register compare where the relational is two calls
([`../../cmd/README.md`](../../cmd/README.md) §2 is the account).

termcap is nothing *but* buffer cursors, so it had four:

| Where | v7 wrote | Now |
| --- | --- | --- |
| `tnchktc()`, the backward scan for the last field's colon | `while (*--p != ':') if (p<tbuf)` | an `int` index walked down over `tbuf[n]` |
| `tgetent()`, the escaped-newline crunch | `if (cp > bp && cp[-1] == '\\')` | `if (n > 0 && cp[-1] == '\\')` |
| `tgetent()`, the entry-too-long bound | `if (cp >= bp+BUFSIZ)` | `if (n >= TBUFSIZ - 1)` |
| `tnchktc()`, the truncation point | `q[BUFSIZ - (p-tbuf)] = 0` | based on `holdtbuf` — see below |

**Subtraction is fine** and is left as v7 wrote it: `b$pdiff` decodes both operands, so
`p - holdtbuf` is a correct character count.

Every one of the four is a *bound* or an *end-of-scan* test, which is what makes them worth a
table. None of them faults when it answers wrongly. A mis-ordered comparison here truncates an
entry, walks off the front of a buffer, or mis-splices a `tc=` — and the caller gets a terminal
description that is merely incomplete. Nothing but reading real entries out of a real database
finds that, which is what [`../test/termcapt.c`](../test/termcapt.c) does.

The same hazard was live in libc, in
[`../libc/stdio/getpass.c`](../libc/stdio/getpass.c) — `if (p < &pbuf[8])` was false on the
first iteration, so `getpass()` returned the empty string, always. Fixed in the same pass; that
file's header has the arithmetic.

## `MAXHOP` is 4, not 32

`tgetent()` holds `char ibuf[TBUFSIZ]` and `tnchktc()` holds `char tcbuf[TBUFSIZ]`, and the two
recurse into each other once per `tc=` indirection. At six characters to the word that is
2 × 1024/6 = 342 words per hop, plus two frames — call it 360.

**The user stack is four pages, 4,096 words, at `070000`.** v7's 32 hops would want 11,500 of
them and blow it in silence. Four is past any real database: the deepest chain in the
`/etc/termcap` this system ships is `xterm-color` → `xterm-r6`, and the deepest *resolvable* one
is `xterm` → `xterm-basic`.

`TBUFSIZ` itself stays 1,024 — the longest entry in the database is `vt100`'s 820 characters,
and `xterm`'s is 844 after the splice. It is spelled `TBUFSIZ` rather than v7's `BUFSIZ`, which
was harmless only because this file includes no `<stdio.h>`.

## `tputs` does not pad

In 4.xBSD `tputs` owns `char PC` and `short ospeed` and emits pad characters proportional to the
capability's delay and the line speed, so that a terminal doing its own cursor motion at 300
baud is not overrun by the next byte.

**Nothing here can be overrun.** The console is a Consul-254 driven a character at a time by
[`kernel/dev/sc.c`](../../kernel/dev/sc.c), which hands the next one over only when
`PRP_CONS1_DONE` says the last has printed, and the SIMH line delivers it as fast as the model
runs. Padding would be dead time.

**The delay is still skipped**, because it is part of the capability string: `cl=50\E[H\E[J` means
fifty milliseconds and *then* the escape sequence, and a `tputs` that did not consume the digits
would print them. But it is only stepped over — v7 converted the digits, the fraction and the
`*` into a number, and there is nothing left here to read one.

The consequence worth knowing: `PC` and `ospeed` are **not defined by this library**, which
leaves both names free. [`../libcurses/curses.c`](../libcurses/curses.c) defines `char PC;` and
[`../libcurses/cr_tty.c`](../libcurses/cr_tty.c) sets `ospeed` from the `sgttyb`, as 4.xBSD had
them, and would collide with a termlib that defined either. Both are written there and read by
nothing, which is the price of the division and is cheap at two words.

## Two v7 bugs fixed rather than carried

* **`tnchktc()` copied the `tc=` name into a 16-character array with an unbounded `strcpy`.**
  A long name in the database overran `tcname` and the stack behind it. It copies up to the
  field terminator and no further than the array now.
* **The inline-`$TERMCAP` branch copied the environment string into the caller's buffer with an
  unbounded `strcpy`.** `strncpy` and an explicit terminator.

And one that is v7's own confusion rather than an overflow: the truncation in `tnchktc()` was
written `q[BUFSIZ - (p-tbuf)]`, but at that point `tbuf` is the **recursive** call's buffer
(`tcbuf`) and `p` points into the outer one — a difference between two unrelated objects. `l`
two lines above was already computed from `holdtbuf`; so is the cut now.

## `/etc/termcap`

[`../../etc/termcap`](../../etc/termcap) is BSD's `termcap.small` **verbatim**, copyright block
included — 4,993 bytes, two blocks of the image. Five entries:

| Entry | Notes |
| --- | --- |
| `cons25\|ansi\|ansi80x25` | 751 characters over 15 continued lines — the longest single entry |
| `vt100\|dec-vt100\|vt100-am\|vt100am\|dec vt100` | 820 characters; the one that carries padding delays, and an alias with a space in it |
| `xterm\|linux\|modern xterm` | 261 characters, and `tc=xterm-basic` — the resolvable chain |
| `xterm-basic\|modern xterm common` | 630 characters |
| `xterm-color` | 104 characters, and `tc=xterm-r6` — **dangling** |

Three things about it are deliberately **not** edited out, because a database is a database and
the port is not the place to improve one:

* **There is no `dumb` entry.** The honest description of this console is whatever terminal SIMH's
  line is attached to; `kernel/dev/sc.c` passes bytes untranslated in both directions, so a host
  `xterm` on the other end honours the `vt100` and `xterm` entries as written.
* **`xterm-color` names `tc=xterm-r6`, which is not in the file.** A dangling `tc=` makes
  `tgetent` return 0 rather than a partial entry, and that is the one failure of `tnchktc()`
  worth having a test case for — so it has one.
* **`vt100` has `if=/usr/share/tabset/vt100`**, a file that is not on the image. Only `tset` and
  `reset` read the `if` capability, and neither has been ported.

## The manual page

[`termcap.3.umm`](termcap.3.umm) is William Joy's, and unlike [`cmd/ls/ls.1`](../../cmd/ls/ls.1.umm) it is
**not** kept as it was. `ls.1`'s only wrongness was a unit; this page's SYNOPSIS declared four
external variables that do not exist here, and three of its statements described code this
implementation does not contain. Each of those is corrected in place, marked **Note:**, and says
what 4.2BSD did as well as what happens now:

* `PC` and `ospeed` are gone, with `tputs` — see above.
* `tgoto` does **not** consult `UP` and `BC` to keep `\n`, `^D` and `^@` out of its result. That
  logic is not in the source we ported; what is left of it appends an always-empty string, so a
  `cm` whose substitution would produce one of those three characters produces it.
* `tgetent` does **not** check `$TERM` before believing an inline `$TERMCAP`. The page said the
  entry is used only when `name` equals `$TERM`; this code matches `name` against the entry's own
  name field, which is what `tnamatch()` is for.

A `BESM-6 NOTES` section carries `MAXHOP` and the unconditional `%` forms, and `FILES` names
`share/besm6/lib` rather than `/usr/lib`. Nothing installs it — no `CMakeLists.txt` in this tree
has a man rule yet — but it is staged onto the image by the top-level `B6_STAGE_MAN` glob, where
manview(1) ([`../../cmd/TODO.md`](../../cmd/TODO.md) C25a) formats it.

## Testing

One program, [`../test/termcapt.c`](../test/termcapt.c), and **it runs twice** — under `b6sim`
(ctest `lib_termcapt`, label `lib`) and off the disk image under the booted kernel (ctest
`libtest_termcapt`, label `kernel`), both diffed against the same
[`../test/termcapt.expected`](../test/termcapt.expected). Under `b6sim` every system call is the
host's, so a kernel bug cannot show; the two disagreeing means one of the harnesses is wrong.
That is [`../README.md`](../README.md)'s argument for the whole libc suite, and it applies with
more force here, since what this library does is `open` a file and `read` it.

**The test owns its environment.** `tgetent` consults `$TERMCAP` before `/etc/termcap`, so a
developer with that variable set would otherwise change what the test reads. `environ` is
`crt0`'s and a plain extern, so the program assigns its own one-entry vector — which also makes
both branches reachable from one program: part 1 puts an **entry** in `$TERMCAP` (a hand-written
`tt` terminal exercising all of `tdecode`'s escape table, the `@` cancellation the shipped
database has no reachable example of, and every `%` form of `tgoto`), part 2 puts a **file name**
there and reads the real entries.

**The database is `argv[1]`**, and that is what lets one expectation serve both runs: `/etc/termcap`
on the image, and the source tree's `etc/termcap` — *the same file* — under `b6sim`, where
[`../test/termcapt.args`](../test/termcapt.args) names it and `run-test.sh` substitutes
`@srcdir@`. It must be absolute; `tgetent` treats a `$TERMCAP` not beginning with `/` as an
entry, which is part 1's case.

**Two branches are deliberately uncovered**, both because their answer differs between the
harnesses and a shared expectation cannot say two things: `tgetent`'s −1 (a `$TERMCAP` naming a
missing file falls back to `E_TERMCAP`, which exists on the image and not on a macOS host), and
the `Termcap entry too long` arms, which want an entry a sixth of the address space long. The
test source says so at the point where they would have gone.
