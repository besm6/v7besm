# manview, and the renderer that does not build a document

Task C25a. Two hundred manual pages have been on this image since the manual went there, and
`man(1)` has been able to find one since C25b. Nothing formatted one. This does.

The source is nobody's: v7 had no renderer for this dialect because it had no dialect, and its
own answer — `nroff` — is not in the v7 source tree at all and is refused along with the whole
typesetting suite in [`../TODO.md`](../TODO.md). What this was written *from* is
[`../man2umm/ummread.cpp`](../man2umm/ummread.cpp), which says in its own header that a renderer
links it and adds a back end.

It could not link it. That file is C++ with `std::string` and `std::vector` in every signature
and a `Doc` of `Block` and `Span` vectors as its result; this is a cross-compiled `a.out` with
28,672 words of address space. So the *grammar* crossed over, function for function — rule 2's
flanking test, the padded backtick, `enum_label()`, `starts_block()`, `take_indented()`'s
blank-line clause — and the document model did not. [`parse.c`](parse.c)'s header says where the
two must agree, and where they disagree `ummread.cpp` is right: it is the half of the converter
that closes the round trip.

## The measurement the whole design rests on

`ummread.cpp` reads a page and builds a tree of it. The largest page here,
[`../sh/sh.1.umm`](../sh/sh.1.umm), is 19,401 bytes, and a span tree over it is several times
that — against 28,672 words for text, data and bss together (§6 of [`../README.md`](../README.md)).

**It does not have to be held.** Measured over all 202 pages in this tree, a *group* — a run of
lines delimited by a blank line that is followed by a line at column 0, fenced blocks excepted —
is at most **4,053 bytes and 71 lines**, and that one is
[`../../lib/libcurses/curses.3.umm`](../../lib/libcurses/curses.3.umm). So `manview` reads one
group, renders it and forgets it, and the buffer is 8,192 bytes: twice the worst case that
exists anywhere in the corpus. A page of any length streams through it.

What the model was carrying is carried by the recursion instead. `parse_blocks()` recurses over
sliced vectors of lines; `blocks()` recurses over `(lo, hi, strip)` index ranges into the one
group, and it can, because **a container's body is stripped by a uniform width** — two columns,
or the `> `, `- `, `: `, `N. ` prefix — which is exactly what `take_item()` slices with. No
array per level, and a 163-word frame.

Two consequences worth writing down, because both were surprises:

* **A paragraph's lines are joined before any of them is looked at**, which is `ummread.cpp`'s
  order and has to be. A bold run crosses a source line in [`../ls/ls.1.umm`](../ls/ls.1.umm)
  and a literal run crosses one in
  [`../../lib/libc/man/getc.3s.umm`](../../lib/libc/man/getc.3s.umm), and a scanner given one
  line at a time can close neither. The join is the NUL between two lines of the group buffer
  overwritten with a space: `push()` lays the lines down contiguously, so there is nothing to
  copy and no second buffer. Threading the font through instead was tried first and handles the
  bold case and not the literal one, which is the tell that joining is the real rule.
* **A quote's marker is blanked in place** for the same reason. Every other container marks its
  first line and indents the rest; `> ` is on every line of the run, so a joined paragraph
  inside a quote would carry a `>` into the middle of its text. Overwriting the two characters
  with spaces turns the run into an ordinary indent, which the recursion already strips.

## What the output looks like, and why

§10 of [`../../doc/Manual_Page_Format.md`](../../doc/Manual_Page_Format.md) is normative and this
implements it: fill a paragraph and nothing else, indent by v7's five, quote a literal run
because the source holds only the delimiters, set a cross-reference with its name in one font and
its parentheses in roman, and **never hyphenate** — a word wider than the measure goes out over
the margin rather than in two pieces.

Three decisions beyond it:

**Attributes are on by default and are not conditioned on `isatty(2)`.** This looks wrong until
the pipeline is written out. `man` builds `manview page | /bin/more` and hands it to `system(3)`,
so this program's standard output is a **pipe** whenever a human is reading; an `isatty()` test
would suppress colour in exactly the case it is wanted and light it up in none. `-p` is the
plain form, and `man - page` is the page source with no formatter at all — which is the meaning
[`../man/README.md`](../man/README.md) gave `-` in C25b precisely so that it would survive this
program's arrival.

**One back end, and it is ANSI.** §10 calls backspace overstrike the default and the fallback;
that is nroff's world, and [`../col/col.c`](../col/col.c) exists to undo it. Nothing on this
machine produces overstrike and nothing but `col` would read it, while [`../more`](../more) and
[`../novi`](../novi) already write hard-coded ANSI/VT100. `-p` is the fallback here and it is a
better one: it is legible in a file, which overstrike is not.

**Attributes are opened and closed around every run** rather than tracked across the output. It
costs a few bytes on a line that alternates fonts — and a word of a multi-word bold run is its
own run, since a word is the fill unit — and it buys the property that matters when the reader
is `more`: no escape sequence outlives the run it belongs to, so nothing can leave a terminal in
an attribute and the output cut at any line boundary leaves both halves well formed.

| element | attribute | colour |
|---|---|---|
| banner, `## HEADING` | bold | yellow |
| `### Subheading` | bold | cyan |
| `**bold**` — what you type | bold | green |
| `*italic*` — what you replace | italic | cyan |
| `` `literal` `` — quoted as itself | underline | — |
| `name(N)` — the name only | italic | magenta |

`-m` keeps the three attributes and drops the colour, for a terminal that has the one and not
the other.

## The corpus is the oracle, and it found four things

Every page in the tree was rendered and its word stream compared against `b6man2umm -t`'s, which
is the same comparison [`../../scripts/mancheck.py`](../../scripts/mancheck.py) makes against
`groff`. It has to agree modulo exactly two things — the quote characters, which are the
renderer's and not the document's, and a list marker, which `word_stream()` does not emit — and
after the four fixes below it does, on all 202.

1. A **bold run crossing a source line**, `ls.1.umm`. The `**` on the second line came out as
   text. This is the paragraph join above.
2. A **literal run crossing a source line**, `getc.3s.umm`, and a `\*` and a `` `end` `` **in a
   heading** (`perror.3.umm`, `string.3.umm`, `getpass.3.umm`, `brk.2.umm`). A `## X` is spans in
   `ummread.cpp` and not a string; here it is one scanner with a *base font*, so unmarked text of
   a heading is in the heading's font and a marked run inside it is in its own.
3. A `>` **landing in the text** of a paragraph inside a quote. This is the marker blanking above.
4. A **fenced block's tab stops measured from the page** rather than from the block. The indent
   is five and a stop is eight, so a hand-aligned table came out with every column shifted by a
   different amount. Measuring from the block's own column 0 shifts the whole of it by five.

The corpus sweep is **not a registered test**. It is worth running by hand after any change to
either file:

```sh
for p in cmd/*/*.umm lib/*/*.umm lib/*/man/*.umm include/man/*.umm; do
    b6sim build/rootfs/usr/bin/manview -p "$p" >/dev/null || echo "FAILED $p"
done
```

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `manview` | 99 | 5,898 | 451 | 3,040 | **9,488** |

Of the 28,672 available, and 42,969 bytes on the disk — 14 blocks, plus 3 for the page, out of
the 204 the image had free before this. There are **187** left.

The bss is the design: 8,192 bytes of group buffer (1,366 words), 2,048 of line buffer, 128 line
pointers, and ~1,030 that everything linking stdio carries. The deepest frame of this program's
own is `out_verbatim()` at 172 words and it does not recurse; `blocks()` is 163 and does, which
is what **`MAXDEPTH` 8** is for — §6's rule about a recursion whose depth the input chooses. The
deepest nesting anywhere in this manual is a leading indent of **three columns**, one container
inside another, so eight is four times what the corpus has ever asked for, and a page past it is
shown as it stands rather than formatted.

## Testing

`ctest -R cmd_manview_` runs the thirteen `b6sim` cases;
[`test/CMakeLists.txt`](test/CMakeLists.txt) states why the fixtures are not named `*.umm` and
why these cases can say nearly everything. That is unusual here and worth stating plainly:
`manview` is a filter with no system call but `open`, `read` and `write` — no environment, no
terminal, no fork — so there is nothing in it that only the booted kernel can reach.

The one thing asserted elsewhere is the **wiring**, and it is section 21 of
`kernel/test/filters.sh`: `man ls` really forks `/bin/sh -c "/usr/bin/manview /usr/man/man1/ls.1"`
there, and the oracle is `manview` run on the same file by hand rather than a checked-in copy of
anything. `man - ls` is still `cmp`'d against the page file itself and is now the only
byte-identity assertion left.

`| /bin/more` has no test and that is a deferral, not an oversight — it is appended only for a
terminal, and a terminal whose output can be diffed is `kernel/test/console`, DISABLED for
`kernel/TODO.md` task 35. [`../more/README.md`](../more/README.md)'s position, word for word.

**One change was made to `more(1)` for this** and it has the same problem. Its column counter
skipped UTF-8 continuation bytes and knew nothing about `ESC`: the escape itself is below `' '`
and cost nothing, but the `[1;33m` after it was seven ordinary characters and was charged seven
columns, so a coloured line folded early. `nextline()` now swallows a CSI run for measuring, in
the same clause and for the same reason the UTF-8 arm is there. Nothing on this image put escape
sequences into a *paged stream* before now, which is why it had never mattered — and it is
unasserted for the reason in the paragraph above it.
