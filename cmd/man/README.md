# man, and the one line that knows what showing a page means

Task C25b. Two hundred pages have been on the image since the manual went there, and until this
nothing on the machine could find one. This finds one, and does not format it: it runs one
formatter over what it found and pipes that into `/bin/more` when standard output is a terminal.
That formatter was `/bin/cat` when this landed — the pages are in the dialect
[`doc/Manual_Page_Format.md`](../../doc/Manual_Page_Format.md) describes and are legible as they
stand — and is [`../manview`](../manview/) since task C25a. `man.c` did not otherwise change,
which is what the first section below is about.

The source is RetroBSD's `src/cmd/man/man.c`, which is Berkeley's 1987 C rewrite. v7 *had* a
`man` — a shell script around `nroff` — and it is not this; the program here is closer to what
`yacc` was, a later Berkeley thing taken because the v7 original wanted a world this machine has
not got.

Four things are worth the file.

## `FORMATTER` is one line, and everything else is arranged so that it stays one line

```c
#define FORMATTER "/usr/bin/manview"    // C25a's renderer, and the only line that knew of cat
```

The display path builds a single command string — `FORMATTER page1 page2 …`, plus `| pager` when
`isatty(1)` — and hands it to `system(3)`. There is no second implementation of "show a page"
anywhere in the program, which is the whole reason `-` does something else.

Upstream's `-` meant *do not page*, and it did so by copying the file out in-process. That
reading worked only while the formatter was `cat`: the day `manview` landed, "do not page" and
"do not format" stopped being the same thing, and a `-` that took the in-process copy would
silently have become the second, wrong one. So `-` is documented and implemented here as **the
page source, unformatted and unpaged** — and that was written before C25a existed, which is why
C25a cost one line. Everything without `-` goes through the formatter, terminal or no terminal.

That split is also what makes the display testable at all. Under `b6sim` there is no exec of a
BESM-6 `a.out`, so `system(3)` reaches nothing; under the booted kernel `man ls` really forks
`/bin/sh -c "/usr/bin/manview /usr/man/man1/ls.1"`, and `kernel/test/filters` compares that
against `manview` run on the same file by hand. `man - ls` is still `cmp`'d against the page file
itself and is now the only byte-identity assertion left, so no checked-in copy of a page can go
stale either way.

## The section search is an ordered table, and the reason is not the one you would guess

```c
static const char *const section[] = { "1","1m","8","6","2","3","3s","3m","3x","4","5","7", 0 };
```

The directory comes off the leading digit, so this is the only list of sections in the program.
`b6sim` cannot read a directory, which is a good reason not to use `readdir(3)` — but it is the
third-best one.

The first is that **thirteen page names live in two sections at once** on this image: `acct`,
`chmod`, `chown`, `crypt`, `intro`, `kill`, `mount`, `nice`, `passwd`, `sleep`, `sync`, `time`,
`write`. A directory walk answers with whatever order the directory happens to hold, so `man
mount` would be a coin toss between the command and the system call. The table's order — v7's
own, commands first — is the thing that decides, and `cmd/man/test`'s `commands` case is what
pins it.

The second is cost: a hit on `man ls` is one `access(2)` and the worst case is twelve, against
opening five directories and reading them.

What the table does *not* survive is a **new subsection letter**. `B6_STAGE_MAN` derives the
directory from the section digit, so a page named `foo.2v.umm` is staged onto the disk correctly
and cannot be found here until `"2v"` joins the list. A new *digit* is already covered: `4`, `6`
and `7` are in the table although those directories do not exist, precisely so that the first
page written for one is findable. The manual page says so under BUGS rather than leaving it to
be discovered.

## What was fixed rather than carried

Four of these are live defects on any machine:

- `if (!(fd = open(fname, O_RDONLY, 0)))` — the descriptor is tested against **0**, so a page
  opened on descriptor 0 is reported as a failure and a *failed* `open` (−1) sails on into
  `read(2)`.
- `-w` sets `WHERE|ALL` and the miss is then tested against `ALL`, so **`man -w nosuch` printed
  nothing and exited 0**. It is now diagnosed and the status is 1; `noent` is the case.
- `man ls nosuch` called `exit(1)` on the miss, **throwing away the `ls` page it had already
  queued** — the command string is built and never run. A miss is recorded and the title list
  runs to the end; `partial` is the case.
- `add()`'s single `realloc(…, buflen += 1024)` is not a loop, and it recomputed the cursor from
  a length it had already advanced.

And one is this machine's, from the list in [`../../CLAUDE.md`](../../CLAUDE.md): the `MANDIR`
table

```c
static MANDIR list1[] = { "cat1", "1st", "cat8", "8th", … };
```

is a **string literal initialising a `char *` inside a struct initializer**, which `b6lower`
cannot do at all. It would have gone anyway — the `cat?` directories do not exist here — but it
is worth recording that the flat array above was not a matter of taste.

`getopt(3)` is not in this libc, so the parser is [`../ls/ls.c`](../ls/ls.c)'s, with one
difference: a lone `-` is an option here and not a file name.

## What is gone, and where it went

`uname()`/`$MACHINE` and the machine subdirectories; the `local`, `new` and `old` sections; the
preformatted `cat?` directories and the `.0` suffix; the `3f` FORTRAN section; the `$PAGER` →
`more -s` rewriting, which walked caller bytes through a 129-entry `<ctype.h>` table to save one
option. And `-f`/`-k`, which exec `whatis(1)` and `apropos(1)`: neither program is on this image
and there is no `whatis` database for one to read. That deletion is worth one line of its own,
since it is the only thing here that could be mistaken for a gap: the database is the cheap half
whenever somebody wants it, because §9 rule 3 fixes the shape of every page's NAME paragraph —
comma-separated names, ` - `, a description — so `/usr/man/whatis` is a `sed` over two hundred
files, and [`../TODO.md`](../TODO.md) C25's table row is where that is booked.

The nineteen `cmd/*/CMakeLists.txt` and `README.md` comments that used to say *"there is no
renderer yet"* were swept when C25a landed, which is exactly what this paragraph asked for when
it forbade sweeping them early: this program is a reader, [`../manview`](../manview/) is the
renderer.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `man` | 83 | 3,442 | 382 | 1,642 | **5,549** |

Of the 28,672 words available, and 26,860 bytes on the disk — 9 blocks, plus 2 for the page, out
of the 216 the image had free before this. The deepest frame is `dotitles()` at 43 words of
4,096; `iobuf[BUFSIZ]` is 512 words and is `static` for that reason, as is the path buffer.

A stdio-free build — `write(2)` for the paths and the diagnostics, no `printf` — would have come
in near `basename`'s size and saved perhaps five blocks. It was weighed and not taken: `perror`
and `fprintf` are what the diagnostics are made of, and five blocks is not what this disk is
short of.

## Testing

`ctest -R cmd_man_` runs the eighteen `b6sim` cases; [`test/CMakeLists.txt`](test/CMakeLists.txt)
states the three things they structurally cannot reach — the default `/usr/man`, the
environment, and everything after `system(3)`. The other half is section 21 of
`kernel/test/filters.sh`, which is where the default path, `$MANPATH` and the fork are asserted,
and it is labelled `weekly`: `make run` sees the cases and none of the boot.

**The pager tail has no test and that is a deferral, not an oversight.** `| /bin/more` is
appended only when standard output is a terminal, and a terminal whose output can still be
diffed is `kernel/test/console`, enabled again now that the dropped-`send` bug is fixed. It goes in when that
comes back — which is word for word [`../more/README.md`](../more/README.md)'s position, and the
same task is why.
