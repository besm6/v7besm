# more

Task C27. Not a port of a v7 command — v7 had no pager — so, like [`novi`](../novi/), it has a
number and no section in [`../TODO.md`](../TODO.md). The source is RetroBSD's
`src/cmd/more/more.c`, Eric Shienbrood's by way of Berkeley. `/bin/less` is a hard link to it.

## The terminal database

`more` is the first user of [`b6_prog()`](../../scripts/BesmCross.cmake)'s `LIBS` keyword, and
takes `libtermcap`; the keyword **emits its own link order rather than the caller's** —
`[-lm] [-lcurses] [-ltermcap]` ahead of `-lc` — because the order is a contract a caller has no
reason to know. It replaced RetroBSD's five termcap stubs, and what the real library changed:

- **`tputs()` takes an output function.** One `putch()` wrapper is handed to it; the two hundred
  `putchar()` calls stay `<stdio.h>` macros. `tput()` beside it is the NULL guard every call site
  used to get for free from a stub that was only ever asked for capabilities it had.
- **Padding is parsed and dropped.** `vt100`'s `cl` is `50\E[H\E[J`; the stub would have printed
  the `50`.
- **`se` is the entry's**, so an xterm gets `\E[27m` (end standout) rather than `\E[m`
  (end *everything*).
- **The buffers are at file scope**: `tgetent()`'s `bp` must outlive the call because the other
  three read the entry it left behind. 1024 chars of bss plus a 512-char decoding arena, and 1024
  is spelled out rather than named — `TBUFSIZ` is private to `termcap.c` and cannot move into
  `<term.h>` without hitting `b6cpp`'s character-identical redefinition rule.

**`$TERM` unset means `xterm`, and so does a `$TERM` the database does not have.** That second
half is not a courtesy: `/etc/termcap` here is BSD's `termcap.small`, five entries of which one
(`xterm-color`) dangles on a `tc=` that was never staged, so an unknown name is the ordinary case.
Only a database that cannot be opened at all leaves the terminal `dumb`.

## Six keys

`ku`, `kd`, `kP`, `kN`, `kh` and `@7` — Up, Down, PageUp, PageDown, Home, End — are read out of
the entry into a small table, and `readkey()` sits in front of `readch()` matching against it:
anything but ESC passes through, an ESC collects bytes until the buffer is some entry's whole
sequence or is no entry's prefix.

The table also carries **built-in ANSI forms, and they earn their place twice over**. `more` never
emits `ks`, so a terminal stays in *normal* cursor mode and a real xterm sends `\E[A`, `\E[H`,
`\E[F` — while the `xterm` entry lists the *application*-mode `\EOA`, `\EOH`, `\EOF` that `ks`
would have switched it to. Emitting `ks` was rejected: there is no way to guarantee the matching
`ke` when a signal this program does not catch can end it, and a table row costs two words.
Separately, the `vt100` entry has no `kP`, `kN`, `kh` or `@7` at all, so under `TERM=vt100` four
of the keys work through the fallback alone.

Each key is an existing command less the part that does not belong on a key: PageDown is space
without `z`'s side effect of making the count the new window size, Down is `<return>` without the
same, PageUp is `b` without its `...back N pages` banner — printed only because a scrolling
terminal has no other way to show that the file moved backwards. Home and End are new. **End is
two passes over the file**, and there is no cheaper way: nothing here indexes lines and
`file_size` is bytes.

`command()`'s `comchar` and `number()`'s argument are `int` — the codes are above `0377` so that
nothing collides with a Cyrillic keystroke, which is the same reason `readch()` answers an `int`.

**What cannot be fixed is the bare ESC.** Telling the key from the start of a sequence needs a
timeout, and there is no `select(2)` and no `VMIN`/`VTIME` under `sgtty`. `more` has no ESC
command, so the cost is one swallowed keystroke and a bell; the manual page says so under BUGS.

## Five letters, for the fingers that learned `less`

`j` and `k` become `K_DOWN` and `K_UP` before the switch and reach those arms unchanged. `g` and
`G` are two more labels on the arm the four backwards keys share, and `u` is `d` backwards,
sharing its `nscroll` so that a count set on either is the new default for both. **The letters
take a count where the keys do not** — `50g` and `50G` both start the screen at line 50, a bare
`g` is Home and a bare `G` is End — which is the whole of `hadcount`, the arm having already
forced the count to 1 by the time it dispatches.

**There is no `^U`.** `CKILL` is `025` (`<sys/tty.h>`), so `^U` is the line-kill character and
`number()` spends it cancelling a half-typed count before the switch can see it; `^B` and `^D`
are here because neither of those is special. `.` repeats all five with their counts and needed
no code at all — `lastcmd` and `lastarg` were already `int` for the keypad.

## The search is literal

`re_comp`/`re_exec` are not in this libc, so `/pattern` is `strstr()` over a saved string. That
reads like a loss and is not, quite: `strstr` compares bytes, so a Cyrillic string typed at the
`/` prompt matches, where `ed`'s engine — the one this would otherwise have grown a second copy
of — indexes a 129-entry `<ctype.h>` table and could not have. What is gone is the metacharacters,
and the manual page says so. Whether `ed`, `sed`, `grep` and `more` should share one engine is
still the open question [`../novi/README.md`](../novi/README.md) left.

## The screen, and the size it is assumed to be

`$TERM` is never set on this image — `login(1)` exports `HOME` and `PATH`, and there is no
`/etc/profile` — so `initterm()` falls to the `xterm` entry and the screen is 24 by 80 whatever
SIMH's line is really attached to. There is no `TIOCGWINSZ` here to ask with. `$LINES` and
`$COLUMNS` are the whole of the override.

Two things follow from the width being a guess. Upstream **omits the newline** after a line that
reached `Mcol` and lets the terminal's automargin wrap instead; when the terminal is wider than
`Mcol` it does not wrap and `--More--` appears at the end of that line rather than under it. So
`nextline()` folds at `Mcol - 1` and `screen()` ends every line itself — which is
[`../manview/render.c`](../manview/render.c)'s rule (`width = w - 1`), so the two programs
`man(1)` pipes through agree instead of differing by a column. `Wrap` and the `am` capability went
with that change. The other is `colflg`, which asked `column == Mcol`: a tab or the `^L` expansion
steps *past* the fold without landing on it, and the physical newline that follows then costs a
blank row. `>=`.

**The window is `Lpp - 1`, not upstream's `Lpp - 2`.** The prompt row is overwritten by the next
screenful's first line, so a prompt costs one row and no more; what upstream held back was the
previous screenful's last line, carried over as context. Dropping it makes `-p` the redraw and
nothing else. `b` and `s`/`f` print a one-row banner, charged to the count, rather than three rows
that had scrolled off before they could be read.

## Machine-specific care

**Plain `char` is unsigned**, so every `char` receiving `Getc()` is an `int` — `skiplns()` and
`command()`'s skip loop spin forever otherwise, and `rdline()` would append up to 511 bytes of
`0377` to the last line of every file, which is the line `search()` then looks at. Conversely
`show()` and `ttyin()` guard their `^X` rendering with `c < ' '`, which unsigned `char` makes
correct: a byte above `0177` is a large positive value and passes through as data.

`b6parse` rejects a local named `ch` while a file-scope `ch` exists — *Duplicate variable
declaration*, not shadowing — which is why `readch()`, `ttyin()` and `expand()` are named as they
are. `prbuf()` walks bytes rather than characters, which is right: it is emitting, not measuring.
Only `nextline()`'s `column` counts characters, and `LINSIZ` is 512 to pay for it.

## Options that cannot be observed

With standard output not a terminal, `main()` takes `copy_file()` and `more` **is** `cat`:
`-c -d -f -l -p -s -u` and `-`*n* are inert, and `$MORE` can only set those same flags. With
standard input not a terminal either, `more.c` clears `firstf` before the file loop, so `+linenum`
and `+/string` are not reached for a named file — and the `::::::::::::::` banner appears even for
a *single* file, which reads like a bug and is not. So the `inert` case passes every option and a
`+5` at once and asserts the output is unchanged, rather than leaving a reader to assume the
options were covered.

## Sizes

|        | const | text  | data | bss   | total      |
|--------|-------|-------|------|-------|------------|
| `more` | 136   | 8,665 | 744  | 1,530 | **11,075** |

11,075 words of the 28,672 — the largest program here that is not a toolchain component. The
database costs 1,555 of them: the library's 1,075 (`termcap.o` 761, `tgoto.o` 206, `tputs.o` 108),
256 words of bss for the entry buffer and arena, and the key table and its matcher. The five
letter commands were free: the help screen they are listed on is two columns where it used to be
one, and the ~95 words of `data` that saved is more than the arms cost.

Stack, from `15 utm N` in `b6cc -S`: `command()` 250 words, `main()` 189, `initterm()` 178,
`ttyin()` 154, of 4,096. The depth that matters is what `tgetent()` costs *below* `initterm()`:
`ibuf[TBUFSIZ]` and `tnchktc()`'s `tcbuf[TBUFSIZ]` are ~342 words a hop, which is why
[`lib/libtermcap`](../../lib/libtermcap/README.md) caps `MAXHOP` at 4 rather than v7's 32. The
deepest chain reachable here — `main` → `initterm` → `tgetent` → one `tc=` hop, which is what
`xterm` needs — is about 1,100 words.

## Testing

`ctest -R cmd_more_` runs the ten `b6sim` cases in [`test/`](test/); `ctest -R rootimg_link_more`
asserts off the finished image that `/bin/less` and `/bin/more` are one inode.

**That the ten cases do not move is the regression signal for the whole termcap half.** With
standard output not a terminal, `initterm()` never reaches `tgetent()` at all, so a `.expected`
file that shifts means the filter half was disturbed by work that should have touched only the
screen half.

The screen half is driven by hand — a pty, `TERM` in the environment, and `b6sim` on the staged
binary:

```sh
TERM=xterm build/cmd/sim/b6sim build/rootfs/bin/more file
```

`=` after each command reports the line reached, which makes the keys checkable: on a 200-line
file whose first screenful ends at 23, Down and `j` give 24, `k` 23, `3j` 26, `d` 37, `u` 26 back,
`50g` 72, `100G` 122, `g` 23, `G` the last screenful — and `.` after `3j` another three. Check
that `k`, `u`, `g` and `G` ring the bell under `cat file | more` where `b` does and `j` still
moves, and that `^U` still cancels a count (`12` `^U` `j` moves one line). Worth running under `xterm` (both the `\E[` and the `\EO` forms), `vt100` (padding stripped, four
keys reaching the fallback), and a name the database does not have (the retry as `xterm`); then
the same file in an 80-column window and a 120-column one, a file with tabs and one with a `^L`
(the two ways `column` steps past the fold), and `!` with a long path followed by `!!`. `LINES`
and `COLUMNS` are exported from the shell, which is the only channel there is.

**The screen half has no automated test.** A single keystroke off descriptor 2 with `ECHO` off and
`CBREAK` on, answered with cursor motion, is harder to assert than any dialogue the tree has ever
had. Unlike `novi` there is no separable module to unit-test in the meantime: `nextline()`,
`prbuf()` and `search()` are pure byte functions but sit in the one translation unit the pager is,
and splitting `more.c` to reach them would be a larger change than the port was.
