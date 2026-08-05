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
is how the two tentative definitions upstream has of that name came to light.

## Left alone, deliberately

`show()` and `ttyin()` guard their `^X` rendering with `c < ' '`, which **unsigned `char`
makes correct**: a byte above `0177` is a large positive value and passes through as data.
That is §11 already satisfied, and it would have been a wild write on a machine with signed
`char`. `prbuf()` still walks bytes rather than characters, which is right — it is emitting,
not measuring; only `nextline()`'s `column` counts characters, and `LINSIZ` went 256 → 512 to
pay for it.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `more` | 124 | 7,549 | 726 | 1,225 | **9,624** |

9,624 words of the 28,672, between `pr` (7,725) and nothing much — the largest program here
that is not a toolchain component. The deepest stack frame is 281 words of 4,096, which is
`command()`; deleting `initterm()`'s `char buf[TBUFSIZ]` took 171 words off that on its own,
and the `static char clearbuf[TBUFSIZ]` beside it another 171 of bss. Both existed only to be
handed to a `tgetent()` and a `tgetstr()` that ignore them.

## Testing

`ctest -R cmd_more_` runs the ten cases; `ctest -R rootimg_link_more` asserts off the finished
image that `/bin/less` and `/bin/more` are one inode.

**The screen half has no test and that is a deferral, not an oversight.** `kernel/test/console`
and `kernel/test/edit` are DISABLED for the `send` wobble of `kernel/TODO.md` task 35, and a
`more` dialogue is harder to assert than either: a single keystroke off descriptor 2 with
`ECHO` off and `CBREAK` on, answered with cursor motion. It goes in when those two come back.
Unlike `novi`, there is no separable module to unit-test in the meantime — `nextline()`,
`prbuf()` and `search()` are pure byte functions but sit in the one translation unit the pager
is, and splitting `more.c` to reach them would be a larger change than the port was.
