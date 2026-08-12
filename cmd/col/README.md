# `col` carries eight bits, and v7's stole one of them

`col` is one of task C5b's seven ([../README.md](../README.md)) and the one deliberate divergence
in the task. [../README.md](../README.md) §10 allows a divergence and asks for it to be
written down twice — in the source and in the manual page; this is the third place, on the
model [../rev/README.md](../rev/README.md) set for task C5a's.

## What was decided

**v7's `col` steals bit `0200` of every stored character.** It exists to post-process the
output of a Model 37 Teletype driver, whose Greek type box is selected by `SO` (`016`) and
deselected by `SI` (`017`), and it tracks that shift state by ORing `GREEK` — which is
`0200` — into each character as it goes onto the page, testing `*p & GREEK` on the way out
and clearing it with `*p & ~GREEK` before the `putchar`. On the way in it masks with
`c &= 0177`, and it admits a character to the page only if `c > 040 && c < 0177`.

**All of that is gone.** `SO`, `SI`, `GREEK`, the `greek` variable, the `gflag` state in
`emit()`, the input mask and the upper bound of the printability test. What is left admits
every byte above `040` except `0177`, so a UTF-8 sequence passes through whole.

## Why, and why not the other way

Since task C11 this system's text is UTF-8 end to end — the console driver, the clists, the
filesystem and `/bin/sh` all carry eight bits ([../README.md](../README.md) §11). A byte
above `0177` is not a flag here; it is part of a letter.

**The failure a faithful port produces is worse than losing the text.** `привет` is twelve
bytes:

```
320 277 321 200 320 270 320 262 320 265 321 202
```

Masked with `0177` those become `P ? Q \0 P 8 P 2 P 5 Q \2`, and the printability test then
drops the two that fell below a space. So v7's `col` prints

```
P?QP8P2P5Q
```

— ten bytes of plausible ASCII, two characters shorter than the six that went in. Not an
empty line, not a diagnostic: **a wrong answer shaped like a right one**, which is the kind
that survives review. `cmd_col_utf8` is the case that pins it.

**The alternative was a parallel flag array**, which is what [`ed`](../ed/README.md)'s `QESC`
prefix is the precedent for: keep the Greek shift and move its bit out of the character into
storage of its own beside `lbuff`. It was rejected because there is nothing on the other end
of it. There is no Model 37 on this machine and no way to attach one; more to the point there
is no *producer* — v7's `col` exists to filter `nroff`, and [../README.md](../README.md)'s
exclusion table records that there is no `nroff` in this source tree at all, only `troff`,
which drives a phototypesetter that does not exist either. Carrying a mechanism whose only
effect here would be to corrupt real text, in service of a device nothing can produce input
for, is not fidelity.

**That last clause had a date on it, and the date has now passed.** [`../manview`](../manview) is
C25a's renderer for the manual pages, and it was once to have v7's own overstrike as its default
back end, which would have given `col` the producer it has never had here. It landed with one
ANSI back end and no overstrike at all, so "no producer" is still true and is now settled rather
than pending — and the day anything emits `c\bc` and `_\bc`, this `col` strips them already. The
Greek shift is not coming back either way.

**So this is the cut [`getty`](../getty/README.md) made to the speed table**, and the one
[../README.md](../README.md) tells `stty` to make to its capability list when C6 comes: delete
what this hardware has not got, rather than keep a mechanism that can only get in the way of
something real. The rule generalises past this program — **a v7 feature that steals a bit
must be re-examined on a machine whose text uses that bit**, and the question to ask first is
not "how do I keep it" but "what still produces input for it".

## The honest half: columns are counted in bytes

`col` aligns text in columns, and a two-byte Cyrillic letter occupies **two** of them. That
is stated in `col.1.umm` rather than fixed.

Making it character-aware would mean decoding UTF-8 in `outc()`, and it would be inventing a
behaviour rather than porting one: the half-line arithmetic this program exists for is about
a *carriage*, and what a carriage advances by is not a question v7 ever had to answer for a
multi-byte letter. It is the same limitation the shell's pattern language has — `?` matches
one byte — and [../README.md](../README.md) §11 already records that as a property of the
system rather than a defect of a program.

`rev` went the other way for task C5a and reversed UTF-8 *sequences*, and the difference
between the two decisions is worth stating because it looks inconsistent and is not:
**`rev`'s whole subject is the order of characters**, so a byte-wise `rev` produces mojibake
and nothing else it could do would be right. `col`'s subject is vertical motion; its column
counting is incidental to that, and the wrong answer is a misalignment rather than a
corruption.

## Three other things the port fixed, none of them a divergence

**`setbuf(stdout, fbuff)` took a real bug with it.** `char fbuff[BUFSIZ]` is an *automatic*
of `main()`, and `BUFSIZ` is 3072 here where it was 512 on a PDP-11 — **512 words out of the
4,096** §6 gives the stack, an eighth of the ceiling *nothing checks*, for a buffer `stdout`
would have allocated from the heap anyway. And `main()` **returns**, after which `exit()`
flushes `stdout` out of a frame that no longer exists. Deleting the array and the `setbuf`
call together fixes both, and the general form is worth keeping: **a `setbuf` into an
automatic is a dangling buffer wherever `main` returns rather than calling `exit`**, and it
is a stack problem here on top of that.

**`outc()` had no bound.** `cp` is advanced by every space, tab and printable character with
nothing limiting it, and the cursor walks `lbuff` to reach it, so a long enough line left the
800-byte buffer — and the overstrike branch opens *two more* bytes each time it fires. `cp`
is clamped through `tocol()` now, and the insert refuses to open room it has not got.
§6's rule, met the way every port in this tree has had to meet it.

**`char *strcpy();` and `char *malloc();`** — the two §1 re-declarations of library functions,
the second of them inside `store()`.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `col` | 87 | 3,500 | 168 | 1,434 | **5,189** |

Out of the 28,672 words §6 allows — and `col` is **the only program in task C5b that
allocates**, which is the interesting number and is not in that table at all. `page[256]` is
a sliding window of `malloc`'d half-lines, up to 256 live at once, and `rootfs_col_size`
weighs `const + text + data + bss` and cannot see a byte of it.

**That is a fourth ceiling on top of §6's three, and like the stack nothing checks it.** The
worst case is real rather than theoretical: 256 entries of `strlen(lbuff) + 2` with a full
800-byte line is 802 bytes each, ⌈802/6⌉ = 134 words, **34,304 words** — past the 28,672 the
address space allows, before the 5,189 above it. What stops that being a fault is that `col`
already checks: a `malloc` returning null prints `col: no storage` and exits 2, so the
failure is a diagnostic and not a wild store. In practice a `page` entry is one half-line of
real text and the window is a fraction of one page.

The general form, and it applies to anything in C10 that manages its own storage: **the size
ctest is a bound on the image, not on the program.** A heap grows from the end of `bss`
through the same 28,672 words, and nothing measures it.
