# `file`, and what a program that classifies bytes has to be told about UTF-8

Task C5f, and the one program of the seven that [../TODO.md](../TODO.md) says gets a
**deliberate change rather than a faithful port**. §1's C11 pass is mechanical and is not
repeated here.

The brief named one change — this machine's magic numbers — and it turned out to be the
smaller of the two.

## The magic numbers, which are what the brief asked for

v7 dispatches on a PDP-11 16-bit `a_magic`:

```c
switch (*(int *)buf) {
case 0410:      printf("pure ");        goto exec;
case 0411:      printf("separate ");
case 0407:  exec: printf("executable");
        if (((int *)buf)[4] != 0) printf(" not stripped");
case 0177555:   printf("old archive\n");
case 0177545:   printf("archive\n");
}
```

Four things about that line are wrong here and only one of them is the constant:

* **`*(int *)buf` would not have worked at all.** A cast from `char *` to `int *` **floors**
  the fat pointer to a word boundary ([../README.md](../README.md) §2's third hazard, and the
  one the compiler's 2026-06-17 fix does *not* cover). The word is assembled from bytes, as
  `od`'s `wordat()` and `getw(3)` assemble theirs — most significant first, which is exactly
  the order `cmd/libaout/fputw.c` writes a header field in.
* **The compare must be `unsigned`.** `FMAGIC` is `02044252323200407` — 48 bits, the `BESM`
  tag plus `0107` — and an `int` here is 41. `<sys/user.h>` carries the same note on `ux_mag`
  and it is the reason `<sys/param.h>` spells both magics with a `U`.
* **`a_syms` moved.** It is word 5, byte offset 30 here; it was word 4 on a PDP-11.
* **And `0411` and `0177555` name nothing on this machine**, so they are deleted rather than
  retargeted — `getty`'s cut to the speed table again.

**`RELFLG` says more than `a_syms` does, and this is the one place the port gains something
v7 did not have.** `a_flag`'s bit 0 is set by the linker on a fully linked file
(`cmd/ld/output.c`) and forced by `strip(1)`, so `FMAGIC` with it is an **executable** and
`FMAGIC` without it is a **relocatable object** — a distinction v7's `file` could not draw at
all, because on a PDP-11 both were `0407` and the only evidence was whether a symbol table
happened to be present. `cmd/file/test` asserts all four kinds against real build artefacts:
`/bin/file` itself, the same file through `b6strip`, `/bin/sh` (`NMAGIC`), and `crt0.o`.

`ARMAG` keeps its value, `0177545`, and changes its width: an archive begins
`00 00 00 00 FF 65`.

## The change the brief did not name, and it is the one a user meets

v7's classifier ends:

```c
notas:
	for (i = 0; i < in; i++)
		if (buf[i] & 0200) { ... printf("data\n"); goto out; }
...
outa:
	while (i < in)
		if ((buf[i++] & 0377) > 127) { printf(" with garbage\n"); goto out; }
```

**Any byte with the eighth bit set means binary.** On this machine that is a claim about the
majority of what the system stores: text here is UTF-8 end to end since task C11 — the console
driver, the clists, the filesystem, `/bin/sh` and `/usr/dict/words` — so a faithful `file`
reports the image's own Russian prose as `data`, and a C source with one Cyrillic identifier
as `c program text with garbage`.

That is `col`'s failure mode and `sort -d`'s: not an error, not an empty answer, but plausible
output that is quietly wrong. So it is **the sixth deliberate divergence**, after `touch`,
`rev`, `col`, `grep -b` and `sort -d`: `utf8ok()` validates the high-bit runs, and

| | v7 | here |
|---|---|---|
| `привет мир` in a text file | `data` | `text` |
| a C source with a Cyrillic identifier | `c program text with garbage` | `c program text` |
| `0377 0376` in a C source | `c program text with garbage` | `c program text with garbage` |

**The last row is the point.** The divergence is not "stop looking at the eighth bit" — it is
"ask whether the eighth-bit runs are *well formed*". A byte that cannot begin or continue a
UTF-8 sequence still means binary, so `file` has not been made blind, only accurate. The
validator rejects the lead bytes `0200`–`0301` and `0365`–`0377`, a continuation that is not
`10xxxxxx`, the two over-long forms, the surrogates and anything past U+10FFFF; a sequence cut
off by the end of the 512-byte window is accepted, half a letter at the end of a buffer being
evidence of nothing.

**And the divergence is sharp** (C5d's rule). Restoring v7's two loops, rebuilding and
re-running gives `rus.txt: data` and `cyr.c: c program text with garbage`; `badbyte.c` says
`with garbage` either way. A divergence whose test would pass either way is not a test, and
this one was checked by making the change and undoing it.

**One consequence worth naming.** A file that is valid UTF-8 but not all ASCII is reported as
`text` rather than `ascii text`, which is simply the honest word: `ascii text` would be a
false statement about it. `english()` is left exactly as v7 wrote it and still answers *no*
for Russian prose — its histogram is of English letter frequencies and there is nothing for it
to say about a language it does not model.

## And `english()` is the one table this port did *not* have to fix

`ct[NASC]` is 128 entries, guarded by `if (bp[j] < NASC)`. With a **signed** PDP-11 `char`
every byte above `0177` came out negative, passed that guard, and incremented `ct[]` **below
zero** — a wild write in upstream v7, in the routine furthest from anybody's attention.
`char` is unsigned here, so the same guard is correct by construction and the bug is gone
without a line being changed.

It is worth recording as the **fourth** shape [../README.md](../README.md) §11 now knows a
character table in: `grep`'s was the right size and stored unmasked; `sort`'s was 256 entries
reached through a `+128` bias; `sed`'s width was written nowhere as a number; and this one is
**a v7 bug that this machine's own type system repaired**. The instruction that follows is not
to "fix" the guard into an `isascii()` and hand the negative range back.

## The assembler tables were the machine leaking in a second time

```c
char *asc[] = { "sys", "mov", "tst", "clr", "jmp", 0 };
char *as[]  = { "globl", "byte", "even", "text", "data", "bss", "comm", 0 };
```

PDP-11 mnemonics and PDP-11 `as` pseudo-ops, used to recognize assembler source. They are
`b6as`'s now — `cmd/as/symtab.c`'s directives and `cmd/as/tables.c`'s Madlen mnemonics — and
`.even` is gone with them, a word machine having nothing to align to. `troff output`
(`buf[0] == '\100' && buf[1] == '\357'`) went too: [../TODO.md](../TODO.md)'s exclusion table
drops the whole typesetting suite, so nothing here produces it.

**The general question is the one this file asks twice: what still feeds the mechanism?** For
the half-shift `col` deleted the answer was nothing; for `pr`'s terminal `chmod` the answer is
`write(1)` in task C6; and for these two tables the answer is `b6as`, which exists — so they
are retargeted rather than cut.

## Twenty bounds checked one byte short

`type()`, `lookup()`, `ccom()` and `ascom()` share one file-scope cursor `i` into
`char buf[512]`, and about twenty `if (i >= in)` tests guard reads of `buf[i+1]` (`ccom`'s
comment scanner) and `buf[j+2]` (the roff guess). `lookup()` walked its keyword off the end
of the buffer outright. It is C5d's wild read in a smaller shape and it was cheap to fix.

`if (buf[i] <= 0)` deserves its own line, because it is the fourth time this tree has met a
`char`'s signedness carrying meaning: with a **signed** `char` it read "a high byte *or* a
NUL — give up on C", and here it means `== 0` alone. That is exactly what this port wants now
that a high byte is text, so the line stays and says so.

## The harness limit this task found

`run-prog-test.sh` captures the program's standard output as **`<case>.out` in the working
directory**. So a fixture named `exe.out` for a case named `exe` is truncated by the
redirection before the program ever opens it — and `file` then reports it as `empty`, which is
a plausible answer that asserts nothing. The five binary fixtures are `execbin`, `stripbin`,
`purebin`, `objbin` and `archbin` for that reason, and [../README.md](../README.md) §9 records
it as the harness's sixth limit. It cost half an hour, and the shape to remember is C5b's:
**ask what the harness will do with a name before choosing it.**

Five of the fixtures are **build artefacts generated by the test's own `CMakeLists.txt`**
rather than checked in, and that is not tidiness: the subject of this port is what a BESM-6
`a.out` looks like, so a checked-in copy would be a snapshot of the thing under test that
nothing keeps in step with it.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `file` | 114 | 4,406 | 339 | 1,121 | **5,980** |

Out of the 28,672 words §6 allows. `data` is 339 because the four keyword tables are pointer
arrays; `bss` is 1,121, of which stdio's buffer is 1,024 and `buf[512]` is 86.
