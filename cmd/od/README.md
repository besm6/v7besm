# `od`'s flags did not change meaning. The machine word did.

`od` is one of task C5b's seven ([../TODO.md](../TODO.md)) and the one that had to be
*designed* rather than ported. Its whole subject is the machine word, and a machine word here
is 48 bits where v7's was 16.

## The decision

**`-o`, `-d` and `-x` go on meaning "the machine word, in this radix".** That is exactly what
they meant on a PDP-11. Nothing about them is redefined; what moved is the width of the
column, because the thing they print got three times bigger:

| flag | v7 (16 bits) | BESM-6 (48 bits) |
|---|---|---|
| `-o` `-w` | 6 octal digits | **16 octal digits** |
| `-d` | 5 decimal | **15 decimal** |
| `-x` `-h` | 4 hex | **12 hex** |
| `-b` | 3 octal per byte | unchanged |
| `-c` | character | unchanged |

**`-w` is a second spelling of `-o`, not a format of its own.** [../TODO.md](../TODO.md)
asked for "a `-w` word dump in 16 octal digits, beside the byte formats", and a synonym is
what delivers that without the one change that really *would* have redefined a v7 flag —
making `-o` byte-sized so that `-w` could be the word. `cmd_od_word` is a diff of two
identical outputs, which exists so that giving `-w` a format of its own later cannot pass
silently.

**The default stays `-o`, and it has a better reason here than v7 had for it.** Octal words
are what `b6as`, `b6ld` and `b6disasm` speak
([../../doc/Assembler_Manual.md](../../doc/Assembler_Manual.md)), so a bare `od` now agrees
with the rest of the toolchain about what a word looks like.

**A line is 12 bytes — two words.** v7's was 16 bytes and eight items; 16 is not a whole
number of six-byte words, and a word view of a 16-byte line would have had to split one.

**The bytes pack most significant first**, which is not a free choice: it is how six
characters occupy a word ([../../doc/Besm6_Data_Representation.md](../../doc/Besm6_Data_Representation.md))
and what [`getw`](../../lib/libc/stdio/getw.c) and `putw` already do. So `od -c` and `od -w`
of the same file agree about which byte is which, and a file written by `putw()` reads back
as its own words. `cmd_od_align` shows both views of one line and the agreement is visible in
it:

```
00000000        2206255433067454        1004110524646455
	110 145 154 154 157 054 040 102 105 123 115 055
```

`110` octal is `0x48`, and `2206255433067454` octal begins `010010 00…` — the same `0x48`, in
the top eight bits of the word. A short group at the end is padded with zero bytes on the
**right**, which is where the unused characters of a partial word are.

## What the PDP-11 word was baked into, and it was more than the ten `long`s

[../TODO.md](../TODO.md) flagged `od` for its ten `long`s. They were the least of it — every
one is a single word here and every cast vanishes. Five other things carried the 16-bit
assumption, and the fourth is the one that mattered.

**`unsigned short word[8]` is eight *words* here and `sizeof` it is 48, not 16.** So the
`fread` that filled it by `sizeof` and the loops that read it by item disagreed by a factor
of six. The line buffer is bytes now and the word views assemble from it.

**`*(char *)&sn` and `*((char *)&sn + 1)`** split an item into exactly two bytes in PDP-11
order. Both the count and the order are wrong here, and the aliasing is gone with them.

**`for (c = 1; c; c <<= 1)`** terminated after 16 iterations on a 16-bit `int` and would take
41 here. Bounded by the highest format bit.

**`putn()` recursed exactly as deep as the field was wide and discarded every digit above
it.** This is the trap the whole file is, and it is worth stating in its own right:

> A value too big for its format did not overflow the column, and did not print a diagnostic.
> It printed its **low** digits and stopped. A 48-bit word through v7's `putn(n, 8, 6)` comes
> out as six octal digits of a sixteen-digit number — a plausible, wrong answer, in a program
> whose entire output is numbers nobody can check by eye.

It prints *at least* the field width now and never fewer digits than the value needs, so a
width that is wrong can only produce a misaligned column and not a wrong number. And the
widths are derived rather than chosen: 48 bits is exactly 16 octal digits, 15 decimal and 12
hex, so nothing can overflow one.

**The address column was 7 octal digits** — 21 bits, about 2 MB, a PDP-11's file — and it
truncated through the same `putn()`. It is 8 digits and cannot truncate.

## Why the oracle is a second implementation

Every `.expected` in [test/](test/) was computed by a Python implementation of the same
packing, written from `od.1.umm` rather than from `od.c`. That is a stronger discipline than the
rest of task C5b needed, and the reason is the truncation above: **a wrong width prints a
number wrong by a factor of eight and looks entirely plausible.** Sixteen octal digits of a
48-bit word is not checkable by eye, and a captured `.expected` would have asserted only that
`od` agrees with itself.

It is the same argument [`mkfs`](../mkfs/README.md) made for `cmp`-ing against `b6fsutil -n`,
one step weaker: `mkfs`'s two implementations are transcriptions of each other, and these two
were written from the manual page.

## Two things left alone, deliberately

**v7's escape set for `-c`** — `\0 \b \f \n \r \t` and octal for everything else. C11 also has
`\a` and `\v`; v7's `od` has neither, and adding them would change what a dump *means* for no
gain. A byte above `0177` falls to the octal default, which is right: `od` is the one filter
in task C5b whose job is to **show** the bytes rather than to carry them, so it is the one
program in the task with no UTF-8 case and no need of one.

**The offset argument sets the radix of the address column** as a side effect — `x`/`0x` →
hex, a leading `0` → octal, a `.` anywhere → decimal. v7's page never mentioned it; `od.1.umm`
now does.

The `b` suffix on an offset is **`BSIZE`** (`../README.md` §4), the same decision `tail -b`
took in this task and `dd` before it: 512 named a PDP-11 disk block and names nothing here.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `od` | 87 | 3,715 | 167 | 1,039 | **5,008** |

Out of the 28,672 words §6 allows. Nearly all of the bss is stdio's `FILE` buffer; `od`'s own
storage is two 12-byte line buffers.
