# `sort`, and the arena that starved stdio without saying so

Task C5d, the sixteenth of the text filters and the last of the ones that were supposed to be
cheap. `../README.md` §1's C11 pass is mechanical and is not repeated here; what follows is
what the port *taught*, worst first.

The brief in [../TODO.md](../TODO.md) predicted the expensive part would be §2 — fifteen
`char *` comparisons inside the routine that decides the whole output. The count was right and
the prediction was wrong: those comparisons compile correctly and cost nothing but time. What
actually cost the day was two things no line count could have named.

## The tables were rotated by 128, and that is a wild read

v7 writes `fold[]`, `nofold[]`, `nonprint[]` and `dict[]` **rotated**: entries 0–127 hold the
values for bytes `0200`–`0377`, entries 128–255 hold the values for `0000`–`0177`. Every use is
biased to match:

```c
struct field proto = { nofold+128, zero+128, ... };   /* v7 */
case 'd':  p->ignore = dict+128;      break;
case 'f':  p->code   = fold+128;      break;
case 'i':  p->ignore = nonprint+128;  break;
```

The rotation exists for exactly one reason: on a PDP-11 a `char` is **signed**, so `code[*pa]`
subscripts with −128…127, and the `+128` puts that range back inside the array.

A `char` here is **unsigned**. So `ignore[*pa]` and `code[*pb++]-code[*pa++]` evaluate
`table[128 + c]` for `c` in 0…255 — that is `table[128…383]`, up to **128 bytes past the end of
a 256-byte array**, for every byte of every Cyrillic letter in the input. It reads whatever the
linker put next.

This is the same family as `grep`'s `CCL` bitmap ([../grep/README.md](../grep/README.md)) and it
is worth being precise about the difference, because the difference is why nobody had found it:
**grep's was a wild *store*** — it corrupted the bytecode it was in the middle of writing —
**and this is a wild *read***. A store leaves wreckage a later test trips over. A read returns a
plausible number and the program carries on.

| | v7 | here |
|---|---|---|
| table layout | rotated 128 | natural order, index `c` holds byte `c` |
| every reference | `table+128` | `table` |
| subscript range | 128…383 for an unsigned char | 0…255, by construction |

**A table indexed by a character wants 256 entries *and* an index that lands in them.** §11 has
said the first half since task C3. The second half is this one's.

## And the rotation was hiding a question: what do `-d` and `-i` mean above `0177`?

Un-rotate the tables and v7's answer becomes readable, and it is *ignore*, in both, for every
one of `0200`–`0377`. Carried faithfully:

```
$ sort -d utf8.txt      # v7's tables
мир
привет
privet
zebra
```

`привет` and `мир` have had every byte deleted before the comparison, so they compare **equal**,
and their order is whatever the sort happened to do on the way past. It is `col`'s failure mode
exactly: not an error, not an empty line, but plausible output that is quietly wrong.

**The fifth deliberate divergence**, after `touch`, `rev`, `col` and `grep -b`: a byte above
`0177` is significant. `nonprint[]` calls it printing and `dict[]` calls it alphanumeric, which
is what it is on a machine whose text is UTF-8 end to end — `look`'s rule, already written down.
`fold[]` stays the identity there: folding the case of a Cyrillic letter is a two-byte operation
and this program has no business doing it.

`cmd/sort/test` asserts the divergence with `utf8dict` and `utf8ignore`, and both cases are
**sharp** — running the same fixture through v7's tables gives a different answer, so a
half-undone divergence fails rather than passing quietly.

**And one line that did *not* have to change, which is worth as much as the ones that did.**
`cmpa()`'s tie-break ends

```c
*pb > *pa ? fields[0].rflg : -fields[0].rflg
```

which on the PDP-11 was a **signed** byte comparison and sorted `0200`–`0377` *below* all of
ASCII. Here a `char` is unsigned and the same line orders bytes by value — and byte order and
code-point order are the same thing in UTF-8, so the unsigned answer is the one a user wants. It
is left exactly as it stands. A diff cannot show that, which is why it is here.

## An arena that takes everything starves stdio, silently

This is the finding the tree did not have, and `find` and `make` will inherit it.

`sort` grabs its arena with `brk()` and **then** `fopen()`s its inputs — up to seven merge
streams and an output. A stream whose `malloc(BUFSIZ)` fails does not fail:

```c
if ((iop->_base = malloc(BUFSIZ)) == NULL) {
        iop->_flag |= _IOUNBUF;      /* lib/libc/stdio/filbuf.c */
        goto tryagain;
}
```

`_IOUNBUF` means **one `read(2)` or `write(2)` per byte**, for the rest of the program's life.
Correct, unboundedly slow, and nothing says a word. v7 had the same shape and accepted the same
consequence; here the merge is the hot path and a boot test has a step budget.

So the arena reserves stdio's buffers before it takes anything — and **reserves them by
allocating them and freeing them**, which is the part worth copying:

```c
p = malloc(STDIORESERVE);
if (p == NULL) { diag("out of memory", ""); exit(1); }
free(p);
```

That forces the break up to cover the reservation, leaves it on `malloc`'s free list where the
nine `fopen`s are the only things that will ask for it, and — the point — **cannot be computed
wrong**. A subtraction would have been: `malloc` grants the break a *page* at a time and serves
one 512-word buffer per page, so subtracting `(N+2)*BUFSIZ` would have reserved barely half of
what nine buffers actually consume, and the failure would have been invisible.

**A reserve that is computed can be computed wrong. One that is allocated cannot be.**

Two more things about the same twelve lines:

* **v7 backed off in 512-byte clicks.** The kernel grants the break a **page** — 1024 words,
  6144 bytes (`pground()` in [../../kernel/sys1.c](../../kernel/sys1.c)) — so the loop asked
  twelve times for each page it gave up. It grows a page at a time now and reaches the ceiling
  in at most 28 calls.
* **`brk(ep -= 512); /* for recursion */` is deleted rather than converted.** On the PDP-11 the
  stack grew *down into* the far end of the data segment, so an arena that took everything left
  the recursion nowhere to go. Here the stack is its own four pages at `070000`, **above** the
  heap's ceiling, and `estabur()`'s `nt + nd > USTKPAGE * PGSZ` is exactly where the break
  stops. The two regions cannot meet. This is the clearest thing in the port that could simply
  be **removed**, and it took reading the kernel to know that.

**And the two worlds do not agree about that ceiling.** `b6sim` refuses a break at `070000`
(`addr >= STACK_BASE`, [../sim/syscall.cpp](../sim/syscall.cpp)) where the kernel refuses one
*above* it, so the simulator's heap is one page smaller and the same input takes a different
number of passes in the two harnesses. Nothing in `cmd/sort/test` or `kernel/test/filters.sh`
may assert a pass count or a temp-file count, and `run-sort-test.sh` says so where it would have
been tempting.

## A `struct merg *` is not a `char *`

```c
qsort((char **)ibuf, (char **)(ibuf+i));      /* v7 */
```

`ibuf` is an array of `struct merg *`, reinterpreted wholesale as an array of `char *`. It works
on a PDP-11 because `l[]` is the first member, so `(char *)mp` and `mp->l` are the same bits.
They are not the same bits here: a `char *` is a **fat** pointer with **bit 48 set** and a byte
offset in bits 47–45, where a pointer to a struct is a plain word address with bit 48 clear
([../../doc/Besm6_Data_Representation.md](../../doc/Besm6_Data_Representation.md)). The fan-in is
`N == 7`, so it is a typed insertion sort now.

**The interesting part is the half of that cast that is invisible at the call site.** v7's
`qsort` does not only sort — under `-u` it also *marks duplicates*:

```c
if (uflg)
        for (k = lp+1; k <= hp;) **k++ = '\0';
```

`**k` reaches `mp->l[0]` only through the same identity, and `merge()`'s next loop reads the
mark back to decide which stream to advance. A replacement that merely sorted would have left
`sort -u` over a **merge** quietly keeping every duplicate — and no case that did not spill to a
temp file would have noticed. The typed sort carries the marking, scanning downward so a line
already marked is never the one compared against.

`compare()` is also **inverted** — a positive answer means the *first* argument sorts earlier,
and both `sortpass()` and `merge()` take the high end of the array first. Anything that "tidied"
that sign would turn the program's entire output upside down. The predicate in the new sort is
copied from the bubble `merge()` already had, for exactly that reason.

## A line limit on one path and not the other is worse than either

v7 truncates a merge line at 512 bytes like this:

```c
if (cp >= ce)
        cp--;             /* v7: overwrite the last byte, for as long as the line goes on */
*cp++ = c;
```

No diagnostic. `sort.1`'s BUGS said "very long lines are silently truncated" and left it there.
But the *sorting* pass has no line limit at all — it writes into the arena unbounded — so the
same file came out right or came out corrupted depending on whether it happened to fit in memory
in one pass, and `sort` merges whenever there is more than one run.

`L` is 3072 now, one `BSIZE`, and **the two paths share it**: a longer line is a diagnostic and
a nonzero exit on both. `toolong` and `toolongmerge` are the two cases, and they exist as a
pair on purpose — this is `grep -b`'s rule from one task earlier, *prefer removing the choice
to making the two choices match*.

The bound was run against before the sentence in `sort.1` was rewritten, which is `grep`'s
correction to itself: 3071 data bytes plus a newline is accepted on both paths and 3072 plus a
newline is refused on both. The first attempt was off by one on the merge path only.

## What else was fixed rather than carried

* **`copyproto()` copied two `char *` through an `int` lvalue**, word by word. An `int` is bits
  41–1; a fat pointer lives in bit 48 and bits 47–45. Every key's `code` and `ignore` went
  through it. It copies the members now, which is what `main()` next door already did.
* **The final line with no newline was thrown away**, twice — `rline()` answered "end of file"
  whatever it had already read, and `sortpass()` supplied the missing newline and then dropped
  the line anyway. The same upstream bug `rev` and `uniq` had. Kept now, in both.
* **Two unbounded writes**, §6's recurring finding: the temp-file name was `sprintf`'d from an
  unbounded `-T` argument into `char[30]`, and the sorting pass wrote a line into the arena with
  no bound inside the line.
* **`setfil()`'s two-letter suffix runs out at 676** and then walks off the end of the alphabet
  — `split`'s 677th-piece bug in another shape. Refused loudly now, in `newfile()`, where the
  refusal cannot recurse into `term()`.
* **`safeoutfil()` indexed `eargv[-7]`** whenever fewer than `N` files were named.
* **`ibuf` was 256 entries** for a fan-in of 7.
* **`isdigit()` on an arbitrary byte** ran off libc's 129-entry table at seven call sites; a
  numeric key is ASCII digits by definition, so the file has its own `digit()`.
* **`/usr/tmp` is gone** from `dirtry[]`: this image has none, so it was a failed `creat(2)` on
  every run. With one entry left, `-T` becomes authoritative — v7 silently used `/tmp` when the
  directory the user named was unusable, which is not a service.
* **v7's private `qsort`** collided with libc's, which `<stdlib.h>` declares. `b6ld` would not
  have complained — an archive member is pulled only for a symbol still undefined — so §1's
  rename-on-sight applies. It is `sortlines()`.

## Left alone, deliberately

`sortlines()` still marks a duplicate under `-u` by poking a NUL into the text arena through the
line pointer, read back by the writer as `if (*cp)`; it works here for the reason it worked
there, a line always having a byte before its newline. `field()`'s `.` case still writes past
`m[2]` into `n[2]` using the distance between them. An unrecognised flag letter is still taken
as a position rather than diagnosed. And the `i < 2` guard added in `merge()` protects a read of
`ibuf[-1]` that is **unreachable** — every path that brings the count to 1 has already set
`muflg` — and it is written down because the reasoning is what makes the line safe and the
reasoning is not in it.

## Cost, measured rather than argued

The fifteen `char *` relationals the brief named are all there. It placed them wrongly:
**thirteen are in `cmp()`**, one is `cp < tspace+ntext` in the sorting pass and one is
`cp >= ce` in `rline()` — `newfile()`, which `../README.md` §2's table credits with some of
them, has none at all. The last two are gone: both loops count bytes with an `int` now, because
they ran **once per byte** where a fat-pointer relational is two out-of-line calls
(`b$pdiff` then `b$lt`), and the arena test only ever needed to be once per line.

The thirteen in `cmp()` stay, and the brief's cost note needs one correction of its own:
`field()` sets `compare = cmp` at the bottom of its loop body, and `c`, `m`, `r` and `t` reach
that bottom by `continue` — so **`sort -r` stays on the cheap `cmpa()` path**, which has no
pointer relational in it at all, and `-u`, `-b`, `-d`, `-f`, `-i`, `-n` and any `+pos` do not.

The suite says the rest: 41 cases, the whole of `ctest -L cmd` still under seven seconds, and
a 4,000-line sort — chosen so that one pass is impossible *by arithmetic*, 188,000 bytes against
an address space of 172,032 — agreeing byte for byte with `LC_ALL=C sort`.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `sort` | 96 | 5,104 | 411 | 1,211 | **6,822** |

Out of the 28,672 words §6 allows — between `login` (6,898) and `sh` (7,928), and the largest of
the sixteen filters by a wide margin. `data` is 411 because four 256-byte tables are 172 words
of it; `bss` is 1,211, of which stdio's two static buffers are 1,024.

**And what that number does not include is the whole of this program.** Everything above the
break is invisible to `rootfs_sort_size`: about 15,000 words of arena, which is more than twice
the image. `col` was the first program whose heap the size ctest could not see; `sort` is the
first whose heap *is the design*. The three run-time floors — the merge slots, the stdio
reservation and one line's worth of text — are checked by the program itself, because nothing
else can check them.
