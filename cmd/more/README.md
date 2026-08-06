# more, and the simulator that had to be told the truth first

Task C27. The second program under [`cmd/`](../) that is not a port of a v7 command — v7 had
no pager at all — so, like [`novi`](../novi/), it has a number and no section in
[`../TODO.md`](../TODO.md). The source is RetroBSD's `src/cmd/more/more.c`, Eric Shienbrood's
by way of Berkeley; the older UCB lineage in the git-ignored `cmd/tmp` was read and not built
from.

Most of the port is the usual list and is in the source. Three things are worth the file.

## The port could not begin until `b6sim` stopped lying about `ioctl`

`initterm()` asks `ioctl(fileno(stdout), TIOCGETP, &otty)` and takes a zero return to mean
*I have a terminal*. Under `b6sim` that call **answered zero for everything** — `SYS_ioctl`
was `sys_ok(0)` with no argument inspected at all — so `more` under any test harness believed
it had a terminal, printed `--More--`, and blocked reading descriptor 2 forever. There was no
way to write a single case.

The fix is not in this directory. `cmd/sim/tty.h` and `cmd/sim/tty.cpp` are new: `gtty`,
`stty` and the terminal half of `ioctl` now translate between the guest's `struct sgttyb` and
the host's `termios`, and `ioctl` mirrors `kernel/dev/tty.c`'s own order — close-on-exec for
any descriptor first, then `ENOTTY` for anything that is not a terminal, then the commands it
implements, then `ENOTTY` again. **This is the rule for every terminal port after this one:
under `b6sim`, `gtty(2)` was always honest and `ioctl(2)` was not.** Both are now.

Two things fell out of doing it. `gtty` was zero-filling **five words into a two-word
structure** — a `char` *member* of a struct occupies one byte, not a word
([`doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md) §4), so
`struct sgttyb` is twelve bytes and b6sim was clearing three words past every caller's buffer.
Nothing had noticed because `isatty(3)`, `getpass(3)` and `ls(1)` all have slack after theirs.
And four documents stated the wrong layout in prose — `ioctl.2.umm`, `ttyname.3.umm`,
`stdio.3s.umm` and `lib/libc/README.md`, the last of which used it to argue that widening
`FILE`'s `_flag` from a `char` to an `int` cost nothing. It costs one word per stream, twenty
over `_iob`; `_iob` really does span 120 words for 20 streams, which is where the number came
from.

The unlooked-for benefit is that `more` and `novi` are now genuinely drivable by hand under
`b6sim` on a terminal. [`../novi/README.md`](../novi/README.md) records having to put the pty
into raw mode *from outside* because `stty` did nothing; that is no longer true.

## An option can be structurally unobservable

Ten `b6sim` cases in [`test/`](test/) and not one of them can test an option, because with
standard output not a terminal `main()` takes `copy_file()` and `more` **is** `cat`:
`-c -d -f -l -p -s -u` and `-`*n* are inert, and `$MORE` can only set those same flags. With
standard input not a terminal either, `more.c` clears `firstf` before the file loop, so
`+linenum` and `+/string` are not reached for a named file — and the `::::::::::::::` banner
appears even for a *single* file, which reads like a bug and is not.

So the `inert` case passes every option and a `+5` at once and asserts the output is unchanged.
That is the shape worth copying: when a branch cannot be reached in a world, say which branch
and why, in a case, rather than leaving a reader to assume the option was covered.
[`../README.md`](../README.md) §9 asks which world a *program* belongs in; this one had to be
asked of a *branch*.

## The terminal database, and the keyword that had to exist first

The port shipped with RetroBSD's five termcap **stubs** — `tgetent()` returning 1, `tgetnum()`
answering `li=25` and `co=80`, `tgetstr()` answering six ANSI literals — not because a database
was unwanted but because there was no way to link one. [`b6_prog()`](../../scripts/BesmCross.cmake)
hard-wired `-lc -lruntime`, and [`lib/libcurses/README.md`](../../lib/libcurses/README.md) had
been predicting a `LIBS` keyword since that library landed. It exists now, modelled on
`b6_libtest()`, and emits **its own order rather than the caller's** — `[-lm] [-lcurses]
[-ltermcap]` ahead of `-lc` — because the order is the contract and a caller has no reason to
know it. `more` is its first user; `novi` is the other program that wants it.

What the real library changed, beyond the two signatures the stubs had shortened:

- **`tputs()` takes an output function**, and `putchar()` here is deliberately the `<stdio.h>`
  macro. One `putch()` wrapper is handed to `tputs()`; the two hundred `putchar()` calls stay
  macros. `tput()` beside it is the NULL guard every call site used to get for free from a stub
  that was only ever asked for capabilities it had.
- **Padding is parsed and dropped.** `vt100`'s `cl` is `50\E[H\E[J`; the stub would have
  printed the `50`. That entry is now usable, which is the whole point of the exercise.
- **`se` is the entry's**, so an xterm gets `\E[27m` (end standout) rather than the stub's
  `\E[m` (end *everything*).
- **The buffers came back**, at file scope: `tgetent()`'s `bp` must outlive the call because
  the other three read the entry it left behind. 1024 chars of bss and a 512-char decoding
  arena, where the stubs cost none — and 1024 is spelled out rather than named, `TBUFSIZ` being
  private to `termcap.c` and unable to move into `<term.h>` without hitting `b6cpp`'s
  character-identical redefinition rule.

**`$TERM` unset means `xterm`, and so does a `$TERM` the database does not have.** That second
half is not a courtesy: `/etc/termcap` here is BSD's `termcap.small`, five entries of which one
(`xterm-color`) dangles on a `tc=` that was never staged, so an unknown name is the ordinary
case. Only a database that cannot be opened at all leaves the terminal `dumb`.

## Six keys, and why the database alone is not enough to read them

`ku`, `kd`, `kP`, `kN`, `kh` and `@7` — Up, Down, PageUp, PageDown, Home, End — are read out of
the entry into a small table, and `readkey()` sits in front of `readch()` matching against it:
anything but ESC passes through, an ESC collects bytes until the buffer is some entry's whole
sequence or is no entry's prefix.

The table also carries **built-in ANSI forms, and they earn their place twice over**. `more`
never emits `ks`, so a terminal stays in *normal* cursor mode and a real xterm sends `\E[A`,
`\E[H`, `\E[F` — while the `xterm` entry lists the *application*-mode `\EOA`, `\EOH`, `\EOF`
that `ks` would have switched it to. Emitting `ks` was the alternative and was rejected: there
is no way to guarantee the matching `ke` when a signal this program does not catch can end it,
and a table row costs two words. Separately, the `vt100` entry has no `kP`, `kN`, `kh` or `@7`
at all, so under `TERM=vt100` all four of those keys work through the fallback alone.

Each key is an existing command less the part that does not belong on a key: PageDown is space
without `z`'s side effect of making the count the new window size, Down is `<return>` without
the same, PageUp is `b` without its `...back N pages` banner — printed only because a scrolling
terminal has no other way to show that the file moved backwards, which a named key does not
need. Home and End are new. **End is two passes over the file**, and there is no cheaper way:
nothing here indexes lines and `file_size` is bytes.

`command()`'s `comchar` and `number()`'s argument became `int` for this — the codes are above
`0377` so that nothing collides with a Cyrillic keystroke, which is the same reason `readch()`
answers an `int`. `lastcmd` and `lastarg` already were, so `.` repeats a key with its count and
needed no change at all.

**What cannot be fixed is the bare ESC.** Telling the key from the start of a sequence needs a
timeout, and there is no `select(2)` and no `VMIN`/`VTIME` under `sgtty`. `more` has no ESC
command, so the cost is one swallowed keystroke and a bell; the manual page says so under BUGS.

## The literal search is better than the regular expressions it replaced

`re_comp`/`re_exec` are not in this libc and were not added, so `/pattern` is
`strstr()` over a saved string. That reads like a loss and is not, quite: `strstr` compares
bytes, so a Cyrillic string typed at the `/` prompt matches. `ed`'s engine — the one this
would otherwise have grown a second copy of — indexes a 129-entry `<ctype.h>` table and could
not have offered that. What is genuinely gone is the metacharacters, and the manual page says
so. Whether `ed`, `sed`, `grep` and now `more` should share one engine is still the open
question [`../novi/README.md`](../novi/README.md) left.

## What else was fixed rather than carried

Four of these are live defects on any machine, not BESM-6 accommodations:

- `write(2, BSB, sizeof(BSB))` where `BSB` is a `char *` — the size of the **pointer**, two on
  a PDP-11 and six here, so every rubbed-out character wrote three bytes past the literal.
  Fixed first and then deleted outright: the arm was reached only under `TIOCLGET`'s `LCRTERA`,
  which this kernel has not got, so `docrterase` was hard-wired 0 and the code unreachable.
  `docrtkill`'s arm in `ttyin()` went the same way. Grep will not find `BSB`; this is why.
- `execute()` declared `(char *cmd, char *args, ...)` and called `execv(cmd, &args)`, taking
  the address of the last named parameter as an `argv[]`. Undefined in C11 and simply untrue
  of this machine's calling convention. The one surviving caller builds the vector.
- `bad_so || (Senter && *Senter == ' ') && promptlen > 0` — `&&` binds tighter, so the column
  guard never applied to `bad_so`.
- `*--sptr = '\0'` in `ttyin()` with nothing read yet, forming a pointer below the array base.

And two are this machine's, both from §11's neighbourhood: **plain `char` is unsigned**, so
every `char` receiving `Getc()` had to become an `int` — `skiplns()` and `command()`'s skip
loop spin forever otherwise, and `rdline()` appends up to 511 bytes of `0377` to the last line
of every file, which is the line `search()` then looks at. `b6parse` also rejects a local named
`ch` while a file-scope `ch` exists — *Duplicate variable declaration*, not shadowing — which
is how the two tentative definitions upstream has of that name came to light. The file-scope
object is now gone as well: it was scratch for `number()` and `colon()` and nothing else, and
each has its own local, which is legal precisely *because* the file-scope name went. The rule
still bites, though, and is why `readch()`, `ttyin()` and `expand()` are named as they are.

Two more that the review of task C27 turned up, both unbounded writes:

- `nextline()`'s loop guard is `i < LINSIZ-1`, but the form-feed arm appends a **second** byte,
  so `i` reaches `LINSIZ` and the terminator writes off the end. It is `LINSIZ-2` here.
- `expand()` `strcpy`s an argv string of any length into a `temp[200]` for every `%`, from a
  78-byte `cmdbuf` that holds thirty-nine of them, and then `strcpy`s the result into a
  `shell_line` of 132. Both halves are bounded now and `temp` is sized to match its
  destination; it cannot be dropped, since the `!` arm reads `shell_line`, which *is* `outbuf`.

And three that only showed on a screen: `error()` accumulated `promptlen` under `-c` because
`cleareol()` does not zero it where `kill_line()` does; `prompt()` left it stale on a hard-copy
terminal, where the prompt is a bell and prints nothing; and `colon()` read with `readch()`, the
one command path bypassing the key matcher, so an arrow typed after `:` left its two trailing
bytes to be read as commands.

## The screen, and the size it is assumed to be

`$TERM` is never set on this image — `login(1)` exports `HOME` and `PATH`, and there is no
`/etc/profile` — so `initterm()` falls to the `xterm` entry and the screen is 24 by 80 whatever
SIMH's line is really attached to. There is no `TIOCGWINSZ` here to ask with. `$LINES` and
`$COLUMNS` are the whole of the override and the manual page now says so out loud.

Two things follow from the width being a guess. Upstream **omits the newline** after a line that
reached `Mcol` and lets the terminal's automargin wrap instead; when the terminal is wider than
`Mcol` it does not wrap, the cursor stays put, and `--More--` appears at the end of that line
rather than under it. So `nextline()` folds at `Mcol - 1` and `screen()` ends every line itself
— which is [`../manview/render.c`](../manview/render.c)'s rule (`width = w - 1`), and the two
programs `man(1)` pipes through now agree on it instead of differing by a column. `Wrap` and the
`am` capability went with the change: margin behaviour is no longer observable either way.

The other is `colflg`, which asked `column == Mcol`. A tab or the `^L` expansion steps *past* the
fold without landing on it, and then the physical newline that follows costs a blank row. `>=`.

**The window is `Lpp - 1`, not upstream's `Lpp - 2`.** The prompt row is overwritten by the next
screenful's first line, so a prompt costs one row and no more; what upstream held back was the
previous screenful's last line, carried over as context. Dropping it makes `-p` the redraw and
nothing else, which is all the manual page now claims for it. `b` and `s`/`f` printed a
three-row banner and *then* a full screenful, so their own banners had scrolled off before they
could be read — one row each now, charged to the count.

## Left alone, deliberately

`show()` and `ttyin()` guard their `^X` rendering with `c < ' '`, which **unsigned `char`
makes correct**: a byte above `0177` is a large positive value and passes through as data.
That is §11 already satisfied, and it would have been a wild write on a machine with signed
`char`. `prbuf()` still walks bytes rather than characters, which is right — it is emitting,
not measuring; only `nextline()`'s `column` counts characters, and `LINSIZ` went 256 → 512 to
pay for it.

`printd()` — a recursive decimal printer whose one caller wanted the digit count — is gone for
`printf("%d", …)`, which the five neighbouring statements that want the same thing already use.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `more`, stubs | 124 | 7,549 | 726 | 1,225 | **9,624** |
| `more`, libtermcap | 133 | 8,691 | 822 | 1,533 | **11,179** |
| after the C27 review | 135 | 8,604 | 827 | 1,530 | **11,096** |

11,096 words of the 28,672 — the largest program here that is not a toolchain component. The
1,555 words the database cost are the library's 1,075 (`termcap.o` 761, `tgoto.o` 206,
`tputs.o` 108), the 1024-char entry buffer and 512-char arena that came back as 256 words of
bss, and the key table and its matcher.

Stack, from `15 utm N` in `b6cc -S`: `command()` 267 words (229 with the stubs — the End case's
temporaries), `main()` 190, `initterm()` 180, `ttyin()` 161, of 4,096. The new depth is not any
of those but what `tgetent()` costs *below* `initterm()`: `ibuf[TBUFSIZ]` and `tnchktc()`'s
`tcbuf[TBUFSIZ]` are ~342 words a hop, which is why
[`lib/libtermcap`](../../lib/libtermcap/README.md) caps `MAXHOP` at 4 rather than v7's 32. The
deepest chain reachable here — `main` → `initterm` → `tgetent` → one `tc=` hop, which is what
`xterm` needs — is about 1,100 words.

## Testing

`ctest -R cmd_more_` runs the ten cases; `ctest -R rootimg_link_more` asserts off the finished
image that `/bin/less` and `/bin/more` are one inode.

**That the ten cases did not move is the regression signal for the whole termcap change.** With
standard output not a terminal, `initterm()` never reaches `tgetent()` at all, so a `.expected`
file that shifts means the filter half was disturbed by work that should have touched only the
screen half.

The screen half itself was driven by hand, which is what §1's `ioctl` fix bought: a pty, `TERM`
in the environment, and `b6sim` on the staged binary —

```sh
TERM=xterm build/cmd/sim/b6sim build/rootfs/bin/more file
```

`=` after each key reports the line reached, which makes the six keys checkable: from line 22 of
a 200-line file, Down gives 23, PageDown 45, Up 44, PageUp 22, Home 22, End the last screenful.
Worth running under `xterm` (both the `\E[` and the `\EO` forms), `vt100` (padding stripped, and
four keys reaching the fallback), and a name the database does not have (the retry as `xterm`).

The C27 review added three more by hand: the same file in an **80-column** window and then a
**120-column** one — before the fold moved, the second welded `--More--` to the end of any line
reaching column 80 — a file with tabs and one with a `^L` (the two ways `column` steps past the
fold), and `!` with a long path and a `!!` after it, for the bounded `expand()`. `LINES` and
`COLUMNS` are exported from the shell, which is the only channel there is.

**The screen half has no test and that is a deferral, not an oversight.** `kernel/test/console`
and `kernel/test/edit` were disabled for the `send` wobble of `kernel/TODO.md` task 35 -- fixed now, and a
`more` dialogue is harder to assert than either: a single keystroke off descriptor 2 with
`ECHO` off and `CBREAK` on, answered with cursor motion. It goes in when those two come back.
Unlike `novi`, there is no separable module to unit-test in the meantime — `nextline()`,
`prbuf()` and `search()` are pure byte functions but sit in the one translation unit the pager
is, and splitting `more.c` to reach them would be a larger change than the port was.
