# Unix Manual Markdown

The format this system's manual pages are written in. It is a **small semantic dialect of
Markdown**, designed for reference material rather than prose, and it replaces the roff `-man`
macros the pages were inherited in.

A page is a plain text file named `<name>.<section>.umm` and living beside the source it documents —
[`cmd/ls/ls.1.umm`](../cmd/ls/ls.1.umm), [`lib/libc/man/read.2.umm`](../lib/libc/man/read.2.umm),
[`include/man/dir.5.umm`](../include/man/dir.5.umm). That placement is
[`cmd/README.md`](../cmd/README.md) §10's rule and it does not change.

**On the image the same file is `/usr/man/man<digit>/<name>.<section>`** — v7's own layout, the
`.umm` suffix dropped, the section digit choosing the directory and the subsection letter left on
the file: `/usr/man/man1/ls.1`, `/usr/man/man1/fsck.1m`, `/usr/man/man3/stdio.3s`. It is staged
from the source file itself, so an edit here is on the disk with no install step.

## The implementation is the specification

**[`cmd/manview`](../cmd/manview/) is definitive.** It is on the image as `/usr/bin/manview`, it
is what [`man(1)`](../cmd/man/) runs over a page it has found, and it is the only formatter this
system has. Where this document and that program disagree, the program is right and this document
is a bug. Every rule below therefore names the code that implements it —
[`parse.c`](../cmd/manview/parse.c) recognizes the dialect,
[`render.c`](../cmd/manview/render.c) puts it on a terminal — so that the next reader can check a
claim against the machine rather than against this file.

Two other programs have a stake in the format and neither overrides `manview`:

- [`cmd/man2umm/ummread.cpp`](../cmd/man2umm/ummread.cpp) is the host-side twin. It reads the same
  dialect into a `Doc` of `Block` and `Span` vectors, which is what closes `b6man2umm`'s
  round trip, and `parse.c` was transliterated from it function for function. The two agree on
  every recognition rule; §10.11 lists the two places their *output models* differ, and in both
  `manview` is the authority.
- [`cmd/man2umm/lint.cpp`](../cmd/man2umm/lint.cpp) checks a page against §9's canonical shape.
  It checks nothing the renderer needs: `manview` will happily format a page that fails every
  rule in that section.

**Why not roff.** A `-man` page is only legible through a formatter, and this project will never own
one: `troff` drives a CAT phototypesetter that does not exist, v7 shipped no `nroff` at all, and
writing one for a 48-bit word-addressed machine to read its own documentation is a poor trade. A
page in this dialect is legible in any editor as it stands, and one renderer can produce a terminal
page, an HTML page and an online index from the same file.

**Why not plain CommonMark.** CommonMark is tuned for prose. It has no definition lists, which is
what four out of five `-man` constructs actually are; it makes `_` a delimiter, which is intolerable
in a document made of `time_t`, `d_ino` and `SIG_DFL`; and it has no way to say "this run is an
argument you replace" as distinct from "this run is bold". This dialect keeps Markdown's *syntax*
and adds the four things a manual page needs.

**And it is not a Markdown implementation.** There is no link syntax, no image syntax, no raw HTML,
no setext heading, no thematic break, no `~~strike~~`, no `_em_`, no `+ ` or `* ` bullet, no pipe
table. A construct this dialect does not recognize is **text**, not an extension and not an error.

Contents:

- [1. The rules that govern everything](#1-the-rules-that-govern-everything)
- [2. Inline markup](#2-inline-markup)
- [3. Cross-references](#3-cross-references)
- [4. Block constructs](#4-block-constructs)
- [5. The page title](#5-the-page-title)
- [6. Sections](#6-sections)
- [7. SYNOPSIS](#7-synopsis)
- [8. Characters and escapes](#8-characters-and-escapes)
- [9. Canonical shape](#9-canonical-shape)
- [10. What a renderer does](#10-what-a-renderer-does)
- [11. The roff correspondence](#11-the-roff-correspondence)
- [12. Converting a page](#12-converting-a-page)

---

## 1. The rules that govern everything

Five rules, and everything in §§2–4 is a consequence of them. The first three are numbered as they
have always been numbered here, because code comments cite them by number
([`cmd/man2umm/emit.cpp`](../cmd/man2umm/emit.cpp) cites rule 2).

### Rule 1. `_` is never a delimiter

Italic is `*x*`, and only `*x*`. A manual page is full of `time_t`, `d_ino`, `ac_comm[10]`,
`SIG_DFL`, `_exit`, `__SYMDEF`; if underscore opened emphasis, every one of them would need
escaping. It does not, so none of them do.

An underscore is not a *delimiter*, but it is still an ordinary character, and rule 2 counts it as
a non-alphanumeric — so `_*u*_` sets `u` in italic between two literal underscores.

### Rule 2. A delimiter is flanked on the outside and closed up on the inside

This governs `*` and `**`, and nothing else. (A literal run has its own rule; see §2.)

An asterisk run **opens** a marked run only if both are true:

- the character *before* it is the start of the line, or is **not** a letter or a digit; and
- the character *after* it exists, and is **not** a space or a tab.

An asterisk run **closes** the marked run it is inside only if both are true:

- the character *before* it is **not** a space or a tab; and
- the character *after* it is the end of the line, or is **not** a letter or a digit.

In one sentence: **the outside of a delimiter must not be alphanumeric, and the inside must not be
blank.** ([`parse.c:390`](../cmd/manview/parse.c#L390); the same test is `ummread.cpp:90`.) An
asterisk that is neither an opener nor a closer is an ordinary character and needs no escape.

| written | what happens | why |
|---|---|---|
| `2**41-1` | text, both asterisks literal | a digit is on the outside of each |
| `a*b*c` | text | a letter is on the outside |
| `*malloc*(2)` | italic `malloc` | opens after a space, closes before `(` |
| `(**-t**)` | bold `-t` | opens after `(`, closes before `)` |
| `_*u*_` | italic `u` | `_` is not alphanumeric |
| `* x*` | text | a space is on the *inside* of the opener |
| `*x *` | does **not** close at the second `*` | a space is on the inside of the closer |
| `*ab*c` | does **not** close | a letter is on the outside of the closer |

The last three are why an unbalanced page fails quietly rather than loudly: a delimiter that fails
its test is consumed as text and the marked run keeps going to the end of the block. §9's rule 7
is the shape a page must have; write balanced runs.

### Rule 3. `\&` is the zero-width joiner

Borrowed from roff, where it means the same thing. It butts two marked runs together with no space
between them, and it produces no character of its own:

```
**b**\&*size*      renders as   bsize   -- bold `b', italic `size', no gap
*manview*\&'s      renders as   manview's
```

**It is the form that always works, and it is the one to write.** Butting the delimiters together
happens to come out right in one direction — `**b***size*` also gives `bsize` — and wrong in the
other: `*a***b**` leaves italic open and swallows the rest of the block, because the closing test
of rule 2 fails against the `b` that follows. Do not rely on the accident; write the joiner.

It is written **only where it is needed** — between two adjacent marked runs, or between a marked
run and an alphanumeric that must touch it. Where one side is unmarked punctuation, rule 2 has
already made the boundary unambiguous.

The joiner works because of how the renderer fills: a *word* is the unit of line-breaking and a
word may cross fonts (§10.1). `\&` emits nothing, so its neighbours stay in one word and wrap
together.

### Rule 4. Markup does not nest

There is one font at a time. Inside a bold or italic run a backtick is an ordinary character and
an asterisk cannot open anything; inside a literal run *nothing* is markup at all
([`parse.c:364`](../cmd/manview/parse.c#L364), [`parse.c:403`](../cmd/manview/parse.c#L403)). So:

```
**bold with *stars* inside**      the stars print
*italic with `tick` inside*       the backticks print
***both***                        not a construct
`open(2)`                         not a cross-reference (§3)
```

A page that wants bold *and* a quoted token writes them as two runs joined by rule 3, or writes the
quotation marks by hand — [`cmd/init/init.8.umm:32`](../cmd/init/init.8.umm) does the second,
escaping two backticks around a bold run because a literal run could not hold one.

### Rule 5. A paragraph is one text, and its source lines are joined before it is read

The lines of a paragraph are concatenated with a single space between them, and only then is the
result scanned for markup ([`parse.c:662`](../cmd/manview/parse.c#L662),
`ummread.cpp:322`). **A marked run may therefore cross a source line**, and dozens of pages do
it:

```
reads back byte for byte as that word's own six characters. v7's assembly, `i |
(getc(iop)<<8)`, would have carried a quarter of the value.
```

— [`lib/libc/man/getc.3s.umm:38`](../lib/libc/man/getc.3s.umm), where a literal run opens on one
line and closes on the next; [`cmd/tr/tr.1.umm:24`](../cmd/tr/tr.1.umm) does the same, and
[`cmd/ls/ls.1.umm`](../cmd/ls/ls.1.umm) does it with a bold run. Joining is not a convenience, it
is the only rule under which either of those can close.

The corollary is the limit: **a heading and a line block are one line each**, so a run cannot cross
a line there. Neither can a run cross a *block* boundary — a blank line, or the start of the next
construct, ends every open font.

---

## 2. Inline markup

Four span kinds, and there are no others.

| span | written | means |
|---|---|---|
| roman | `text` | ordinary prose |
| bold | `**text**` | type this literally: a command, a flag, a keyword, a file name in running text |
| italic | `*text*` | replace this: an argument, a variable, a metasyntactic name; also a first mention |
| literal | `` `text` `` | a character or short token **quoted as itself** — v7's `` `text' `` |

A cross-reference (§3) is a fifth kind in the *document model* and in the renderer's font table,
but it is written with no markup at all, so it is not a span a page author opens and closes.

### Bold against italic

The distinction is the one a `-man` page has always drawn in a SYNOPSIS and it carries meaning, not
weight: **bold is what the user types, italic is what the user substitutes.** A renderer may colour
the two differently, and this one does (§10.8).

### The literal run

A backtick opens a literal run **wherever it stands in roman text**, and the run closes at the next
backtick in the same block ([`parse.c:364`](../cmd/manview/parse.c#L364)). Rule 2's flanking test
does not apply to it — `` a`b`c `` is a literal `b` between two ordinary letters — because there is
no ambiguity to resolve: a backtick has no other meaning in prose.

Three properties follow, and all three are load-bearing:

1. **The body is raw.** No escape is processed, no markup is recognized, no cross-reference is
   found inside a literal run ([`parse.c:384`](../cmd/manview/parse.c#L384)). This is what lets
   [`cmd/ed/ed.1.umm:213`](../cmd/ed/ed.1.umm) write `` `-\b>` `` and
   [`cmd/sed/sed.1.umm:40`](../cmd/sed/sed.1.umm) write `` `\n` `` with the backslash intact, and
   it is why `` `printf(3)` `` is not a reference.
2. **A backtick inside a literal run needs the padded form.** Write the delimiters doubled and pad
   the body with one space on each side; exactly one leading and one trailing space is stripped
   ([`parse.c:365`](../cmd/manview/parse.c#L365)):

   ```
   `` a`b ``        renders as   `a`b'
   ```

   This is also what `b6man2umm` emits when it converts a v7 quotation containing a backtick.
3. **A run that is never closed is not a run.** The backtick stands as an ordinary character.

**A literal run is where v7's quoting went.** v7 wrote a backquote, the text and an apostrophe —
`` `environment' ``, `` `cat a b >a' ``, `` `-' `` — to mean *this, as itself*, which is exactly
what a literal run means. So it is one construct here rather than two punctuation characters, and
**the renderer supplies the quotation marks** (§10.4): the source holds only the delimiters. The
corpus had 330 of them; recognizing the construct rather than transliterating it is what keeps
`` \` `` out of the prose.

---

## 3. Cross-references

**A cross-reference is written with no markup at all**, as `name(N)`:

```
See open(2) and the discussion in ls(1).
malloc(3), free(3), realloc(3)
```

This is the dialect's single most important extension. Roff spelled the same thing
`.IR open (2),` — two macros and a hard line break for a construct that appears about twelve hundred
times across the corpus. Here it is a plain token that reads correctly with no renderer at all.

### The grammar

```
name  := [A-Za-z_] [A-Za-z0-9_.+-]*
xref  := name "(" [1-8] [a-z]? ")"
```

No space anywhere in it. ([`parse.c:290`](../cmd/manview/parse.c#L290); the same grammar is
`escape.cpp:315`.) Two further conditions decide whether a match is taken:

- **The name must start at a word boundary** — the character before it must be neither
  alphanumeric nor `_` ([`parse.c:419`](../cmd/manview/parse.c#L419)). Without this, `sprintf(3)`
  would match at `printf` and lose its first two letters.
- **The scan is in roman text only.** A reference is looked for as the text is read, so a `name(N)`
  inside a bold, italic or literal run is not one.

| written | is it a reference? |
|---|---|
| `open(2)` | yes |
| `fsck(1m)` | yes — the subsection letter is **lowercase** |
| `sprintf(3)` | yes, the whole name |
| `_exit(2)` | yes — a name may begin with `_` |
| `x.y(2)`, `a_b(3s)`, `a+b(1)` | yes — `.`, `_`, `+`, `-` are name characters |
| `FSCK(1M)` | **no** — the letter must be lowercase |
| `open(9)` | no — the section digit is `1`–`8` |
| `open (2)` | no — no space is allowed |
| `open(2` | no |
| `9open(2)` | no — a name may not begin with a digit |
| `` `printf(3)` `` | no — a literal run's body is raw |
| `**open(2)**` | no — the scan is roman-only |

**The letter's case is not the same as a title's.** In a reference it is lowercase, `fsck(1m)`; in
the level-1 title of §5 it is uppercase, `# FSCK(1M)`; and the file is named `fsck.1m.umm`. All
three are deliberate and each is checked in its own place.

### Writing something that is not a reference

Where a page means the literal text — a C function type, a shell word — put it in a literal run:
`` `printf(3)` ``. That is the sanctioned form and it is what the corpus uses.

### A cross-reference must not carry inline markup

Write `open(2)`, never `*open*(2)` or `**open**(2)`. A marked-up reference cannot be recognized and
loses its link: `manview` renders `*malloc*(2)` as an italic word followed by roman parentheses,
which *looks* almost right and is not a reference at all.

Canonical-shape rule 9 (§9) states the prohibition. Be aware that the lint catches only the fully
enclosed spelling `**open(2)**`: `mark_xrefs` (`escape.cpp:449`) suppresses a match only when the
whole of it is uniform in one font, so `**open**(2)` — bold name, roman parentheses — slips past
both the suppression and the check. The rule is the author's to keep.

---

## 4. Block constructs

Nine kinds. The table below lists them in the order
[`blocks()`](../cmd/manview/parse.c#L491) tests for them, because that order is what decides an
ambiguous line: the first match wins. The subsections after it are in the order a page author
meets them.

Two rules govern all nine.

### The group, and how far a block extends

The reader works on one **group** at a time: a run of lines delimited by a blank line *that is
followed by a line at column 0*, fenced blocks excepted
([`parse.c:153`](../cmd/manview/parse.c#L153)). A blank line followed by an *indented* line does
not end anything — it stays inside the group as a blank line of its own.

This is the whole mechanism behind "a definition body may hold more than one paragraph". A body is
whatever is indented under its marker, blank lines and all, and it ends at the first line that is
neither indented nor preceded by more indented text
([`body_end()`](../cmd/manview/parse.c#L239)) — which is what stops a definition swallowing the
next `## SECTION`.

**Indentation is spaces.** A tab in column 1 is not indentation and does not continue anything;
tabs are permitted inside a fenced block and nowhere else (§8).

### Blank lines decide the spacing

A block does **not** need a blank line before it: a `- `, `: `, `> `, `| `, `N. `, `## `,
` ``` ` or `<!-- ` line ends the paragraph above it where it stands
([`starts_block()`](../cmd/manview/parse.c#L273)), and so does the *term* line of a definition
list.

What a blank line does instead is set the vertical spacing. **Two sibling blocks written with
nothing between them are set with nothing between them; two written a blank line apart get a blank
line** ([`parse.c:516`](../cmd/manview/parse.c#L516)). This is the only thing left that can tell a
compact list from a spaced one, since §11 records that roff's `.PD` and `.ns` were dropped and this
dialect has one list form.

### The nine kinds

| kind | trigger, at the block's own column 0 | how far it runs | continuation indent |
|---|---|---|---|
| fenced block | three backticks | to the next line beginning with three backticks | — |
| comment | the five bytes `<!-- ` | that one line | — |
| heading | `## ` or `### ` | that one line | — |
| line block | `\| ` or a line that is exactly `\|` | the maximal run of such lines | — |
| quote | `> ` or a line that is exactly `>` | the maximal run of such lines | 2 (the marker) |
| bullet list | `- ` | that line and everything indented under it | 2 |
| ordered list | one or more digits, `.`, one space | that line and everything indented under it | the label's width: 3 for `1. `, 4 for `10. ` |
| definition list | any line whose **next** line begins `: ` | the `: ` line and everything indented under it | 2 |
| paragraph | anything else | to a blank line, to a line that starts a block, or to the line before a definition term | — |

The level-1 title `# ` is recognized in the same dispatch but is not a block in this sense; §5 has
it.

### Paragraph

Ordinary filled text. Line breaks inside a paragraph are not significant — the lines are joined
with one space by rule 5 and the renderer refills to the output width. Source lines should be
wrapped near 95 columns.

A paragraph always consumes at least its first line, so a construct this dialect does not recognize
degrades to text rather than to an error.

### Line block

Lines beginning `| `, or lines that are exactly `|`. Each source line becomes exactly one output
line, and inline markup is live inside it.

```
| **cat** [ **-u** ] *file* ...
| **ls** [ **-1ACFRacdfgilqrstu** ] *name* ...
```

This is the construct for anything that is both **formatted and line-broken**: a command synopsis, a
short table with emphasis in it, a run of related declarations. It is the only construct that
carries fonts *and* hard line breaks; a fenced block carries breaks but no fonts, and a paragraph
carries fonts but no breaks.

- Leading spaces after the `| ` are preserved, so a line block can be used for light column work.
- A bare `|` is an **empty line** inside the block, not an empty block.
- `|x`, with no space, is not a line block — it is a paragraph.
- Rule 5 does not apply: each line is scanned on its own, so a marked run may not cross one.

### Fenced block

Verbatim text between three-backtick fences. **No markup is interpreted inside**; every character
is literal, and tabs are permitted here and nowhere else. An info string may name the language:

````
```c
struct direct {
    ino_t d_ino;
    char  d_name[DIRSIZ];
};
```
````

- Use `c` for C declarations, no info string for shell transcripts, output samples and tables. `c`
  is the only info string the corpus ever uses, 125 times.
- **The info string is advisory.** `manview` discards it; ` ```c ` and ` ``` ` render identically.
  A back end that has fonts may use it.
- A fence opens and closes wherever it stands — at column 0 at the top level, indented under a
  definition. The closing fence is the next line beginning with three backticks; an unterminated
  fence simply runs to the end of the group and is not an error.
- Nothing inside a fence ends a group, so a fenced block may contain blank lines and lines at
  column 0 freely.

Fenced blocks are the right home for anything column-aligned, since the columns survive by
construction (§10.6 has the tab rule that makes that true).

### Definition list

A term on its own line, then one or more definitions each introduced by `: `. Continuation lines
are indented two spaces.

```
**-l**
: List in long format, giving mode, number of links, owner, size in bytes,
  and time of last modification for each file.  (See below.)

**-C**
: Force multi-column output; this is the default when the output is a terminal.
```

A definition may hold more than one paragraph, and may hold nested blocks; everything after the
first line indents two spaces:

```
**-u**
: Do not buffer the output.

  Buffering is otherwise in 3072-byte filesystem blocks, and is bypassed
  anyway when the standard output is a terminal.
```

Two details:

- **The term and its `: ` must be adjacent.** No blank line between them, and the term line must
  not itself begin a block — a term is recognized only by what the *next* line looks like.
- **A `: ` line with no term above it is still a definition**, with an empty tag. That is how a
  second definition under one term is written, and how a bare indented body is written.

This is the workhorse. Options, error numbers, `FILES` entries, structure members and the tagged
paragraphs of a `DESCRIPTION` are all definition lists. §10.2 has how the tag is set.

### Bullet list and ordered list

```
- the first thing
- the second thing

1. the first step
2. the second step
```

An ordered label is one or more digits, a period and one space; `1.` alone at the end of a line is
not one.

**The continuation indent is the marker's own width**: two spaces under `- `, three under `1. `
through `9. `, four under `10. ` and beyond. Lists nest by indenting a whole item that far.

### Quote

Lines beginning `> `, or lines that are exactly `>`. An indented display that is not a definition —
a worked example, a short transcript, an aside set apart from the running text.

```
> cat file1 file2 >file3
```

The marker is on *every* line of a quote, not just the first, and two columns are stripped from
each. Quotes may contain any other block, indented under the `> ` —
[`cmd/diff/diff.1.umm:20`](../cmd/diff/diff.1.umm) puts a line block inside one and
[`lib/libc/man/ctime.3.umm:49`](../lib/libc/man/ctime.3.umm) a fenced block.

### Comment

```
<!-- UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. -->
```

Not rendered. Provenance notes live here, at the top of the file, above the title.

- The trigger is the five bytes `<!-- ` exactly. `<!--x` is a paragraph.
- **A comment is one line.** There is no multi-line form; a second line would render as text.
  A page that needs several writes several, one per line, and
  [`cmd/od/od.1.umm:2`](../cmd/od/od.1.umm) stacks three.

### Heading

`##` for a section, `###` for a subsection, each one line, each with inline markup live inside it.
§6 has them.

---

## 5. The page title

Exactly one level-1 heading, the first thing in the file after any comments:

```
# LS(1)
```

Grammar:

```
# NAME(SECTION)
# NAME(SECTION) [extra]
```

- **NAME** is the page's principal name, upper case by convention, matching the file's stem.
- **SECTION** is a digit `1`–`8` with an optional **uppercase** letter: `1`, `2`, `3`, `1M`, `3S`,
  `3X`, `3M`, `5`, `8`. It must match the file's section suffix, which is lowercase —
  `# FSCK(1M)` lives in `fsck.1m.umm`, and `lint.cpp` compares the two case-insensitively for
  exactly that reason. A cross-reference to the same page is lowercase again, `fsck(1m)` (§3).
- **`[extra]`** is an optional free-text qualifier in square brackets, split off at the first
  ` [` when the line also ends in `]`. Seven pages carry one:

  ```
  # GETPW(3) [deprecated]     # MAN(1) [April 19, 1988]
  # LOCK(2) [local]           # MORE(1) [October 22, 1996]
  # PHYS(2) [PDP11]           # TERMCAP(3) [May 15, 1985]
                              # CURSES(3) [April 23, 1986]
  ```

There is no front matter. Seven pages out of the two hundred here want a fourth field and a bracket
serves them; a YAML block at the head of every page to serve seven is a poor trade.

The section letters mean what they have always meant here: `1M` a maintenance command, `3S` the
standard I/O library, `3M` the mathematical library, `3X` a routine that is in neither, `5` a file
format, `8` a program run by the system rather than by a user. The corpus is 75 `1`, 44 `2`,
32 `3`, 15 `3S`, 15 `1M`, 10 `5`, 6 `3M`, 5 `8`, 1 `3X`.

**The title is not rendered as text.** It becomes the page banner — the name at both margins with
the manual's name between them, and the qualifier centred beneath. §10.3 has the layout.

---

## 6. Sections

`## HEADING` — upper case, verbatim, one blank line above and below.

Every page has `NAME` and `DESCRIPTION`, and `NAME` comes first. Beyond that, **section order is not
prescribed**: [`lib/libc/man/read.2.umm`](../lib/libc/man/read.2.umm) puts `SEE ALSO` before
`DIAGNOSTICS` and that is how v7 wrote it.

The conventional vocabulary:

| section | content |
|---|---|
| `NAME` | see below — the one section with a fixed body |
| `SYNOPSIS` | how it is invoked or declared; see §7 |
| `DESCRIPTION` | what it does |
| `OPTIONS` | when the flags are too many to sit inside `DESCRIPTION` |
| `FILES` | the paths it reads and writes |
| `DIAGNOSTICS` | what it says when it fails, and its exit status |
| `SEE ALSO` | a comma-separated list of cross-references, nothing else |
| `BUGS` | v7's own word for known limitations, and it is kept |
| `AUTHOR` | where v7 named one |
| `ASSEMBLER` | §2 and §3 pages: the calling sequence from assembly |
| `BESM-6 NOTES` | **this port's own section**: what this machine changed and why |

`lint.cpp` also knows `NOTES`, `EXAMPLES`, `LIMITS`, `ENVIRONMENT` and `EXIT STATUS`. Anything else
draws a warning and nothing more.

`BESM-6 NOTES` is the house convention and it is load-bearing — sixty-seven pages carry one. It is
where a divergence from v7 is recorded at the point a reader will look for it. A page may also
invent a prose heading for something that deserves its own place: `## HOW MANY FILES`,
`## BUFFERING`, `## BLOCKS ARE 1024 BYTES`. That freedom is deliberate, fifty-seven headings use it,
and the lint only warns.

### The NAME section

One paragraph, in one fixed shape:

```
## NAME

ls - list contents of directory
```

```
## NAME

malloc, free, realloc, calloc, aligned_alloc - main memory allocator
```

Comma-separated names, then ` - ` — **one ASCII hyphen, one space either side** — then a one-line
description with no trailing period. This is the line an index and a `whatis` database are built
from, so its shape is checked rather than merely recommended.

The page's own name should be among those listed, but need not be: a page named for a *topic*
lists the members of that topic instead, and [`lib/libc/man/exec.2.umm`](../lib/libc/man/exec.2.umm)
lists `execl, execv, execle, …` without listing `exec`. The lint warns and does not fail.

### Subsections

`### Title` — sentence case, used inside a long section:

```
## BESM-6 NOTES

### Where the BUSY bit lives

...

### The break is granted a page at a time
```

A subsection is set three columns in, against a section's zero (§10.2).

### Markup inside a heading

**Inline markup is live in a `##` and a `###`**, and six pages use it:

```
### `kill -1 1` does not bring the system down here
```

— [`cmd/kill/kill.1.umm:43`](../cmd/kill/kill.1.umm). A heading is scanned exactly as a paragraph
is, with one difference: **unmarked text takes the heading's own font, and a marked run inside it
keeps its own** ([`parse.c:315`](../cmd/manview/parse.c#L315)). So the line above sets
`kill -1 1` as a literal run — quoted and underlined — inside the bold cyan of a subsection.

Rule 5 does not apply — a heading is one line — and the trailing whitespace of the line is trimmed.

### Heading levels

Levels may not skip, and **there is no `####`**. That is not enforced by the reader so much as
arranged: `#### X` matches no heading trigger and falls through to being a paragraph beginning with
four literal `#` characters, which is unmistakable in the output.

---

## 7. SYNOPSIS

Two shapes, and which one to use is decided by what the page documents.

**A C interface gets a fenced `c` block.** There are no font distinctions to make, and the
declaration should be copy-pasteable:

````
## SYNOPSIS

```c
#include <unistd.h>

int read(int fildes, char *buffer, int nbytes);
```
````

Prototypes are ANSI C11 as this system actually declares them — not v7's untyped originals.

**A command gets a line block**, because bold and italic carry the meaning:

```
## SYNOPSIS

| **ls** [ **-1ACFRacdfgilqrstu** ] *name* ...
```

The conventions inside a command synopsis are v7's:

| notation | meaning |
|---|---|
| `**-x**` | type it exactly |
| `*file*` | replace it |
| `[ ... ]` | optional |
| `...` | repeatable |
| `\|` between alternatives | choose one |

Square brackets and ellipses are literal characters here, not markup — there is no link syntax in
this dialect, so `[` never begins one.

---

## 8. Characters and escapes

**The source is UTF-8** and needs no declaration. This machine is byte-transparent end to end
([`cmd/README.md`](../cmd/README.md) §11), so a Cyrillic comment, a `×` or a `—` may be written as
itself; seventeen pages here are not pure ASCII and nine of those have Cyrillic in them. Roff needed
`\(mu` and a `-Kutf8` flag; nothing here does. The renderer counts a *character* and not a byte
(§10.7), so such a page fills correctly.

**A backslash makes the character after it literal, and is itself dropped.** `\&` is the one
exception: it produces no character at all (rule 3). A backslash at the very end of a line is an
ordinary backslash.

### The inline set

These are the characters that ever need it inside running text, and only where rule 2 or §2's
literal-run rule would otherwise make them markup:

| escape | for |
|---|---|
| `\*` | a literal asterisk that would open or close italic |
| `` \` `` | a literal backtick |
| `\\` | a literal backslash |
| `\&` | the zero-width joiner of rule 3 — it is not a literal `&` |

`_`, `[`, `]`, `<`, `>`, `~`, `&`, `(`, `)`, `#`, `:`, `-`, `|` are **never** escaped mid-line.
This is what keeps a raw page readable.

### The column-1 set

A character that begins a block (§4) needs protecting when a *continuation* line of a paragraph
happens to start with it. This is the second escape set and the corpus uses it on a dozen pages:

| escape | what it would otherwise start | seen in |
|---|---|---|
| `\-` | a bullet list | [`cmd/ln/ln.1.umm:33`](../cmd/ln/ln.1.umm), [`cmd/mv/mv.1.umm:28`](../cmd/mv/mv.1.umm), and six more |
| `\.` after digits | an ordered list | `60\.` [`cmd/time/time.1.umm:31`](../cmd/time/time.1.umm), `1968\.`, `04755\.` |
| `\#` | a heading | `\#0` [`lib/libc/man/qsort.3.umm:51`](../lib/libc/man/qsort.3.umm) |
| `\*` | italic, by rule 2 | `\***` [`lib/libc/man/string.3.umm:109`](../lib/libc/man/string.3.umm) |
| `\|` | a line block | — |
| `\>` | a quote | — |
| `\:` | a definition | — |

The cleaner fix is usually to rewrap the paragraph so the character is not in column 1. Reach for
the escape when the sentence reads better as it stands.

### Where escapes are inert

**Inside a fenced block and inside a literal run, a backslash is an ordinary character.** Both
bodies are copied out as they stand. That is what makes

```
`\n`        renders as   `\n'
`-\b>`      renders as   `-\b>'
```

work, and why a page that means a literal backslash *in prose* writes `\\` —
[`cmd/od/od.1.umm:31`](../cmd/od/od.1.umm) writes `\\0`, `\\b`, `\\n` for exactly that reason while
[`cmd/sed/sed.1.umm:40`](../cmd/sed/sed.1.umm), quoting inside a literal run, writes `` `\n` ``.

### What the roff special characters became

Written as themselves, in UTF-8:

| roff | here | | roff | here |
|---|---|---|---|---|
| `\(em` | `—` | | `\(sc` | `§` |
| `\(*p` | `π` | | `\(eq` | `=` |
| `\(mu` | `×` | | `\(aa` | `´` |
| `\(+-` | `±` | | `\(fm` | `′` |
| `\(pl` | `+` | | `\(**` | `*` |
| `\(bv` | `\|` | | `\(rq` `\(lq` | `"` |

Two deliberately stay ASCII. **`\(mi` is `-`**, not U+2212: every use of it is arithmetic prose
("returns -1"), and a typographic minus there is a gratuitous change to text a program's output must
match. **`\(or` is `|`**, not U+2223: every use is a shell pipe or a grammar alternation, and both
are ASCII characters in the thing being described.

**Superscripts are `^`** — `10^9`, `2^15`, `x^y`. Roff's `\u`/`\d` had no ASCII rendering at all and
the three sites that used them printed nonsense on a terminal.

---

## 9. Canonical shape

The shape a page must have. This section is the **author's** contract, not the renderer's:
`manview` implements none of it and will format a page that breaks every rule.

`b6man2umm -l` is what checks it, and
[`cmd/man2umm/test/CMakeLists.txt`](../cmd/man2umm/test/CMakeLists.txt) registers one `man_lint_*`
ctest per page over all 203 in the tree. **Five of the ten are checked in full, two in part, and
three not at all** — the ✗ rows below are worth a second look during review, because nothing else
will catch them.

| | rule | checked |
|---|---|---|
| 1 | Zero or more comment lines, then **exactly one** level-1 heading matching §5's grammar, and no other level-1 heading anywhere in the file. | partly — the reader errors if the first non-comment line is not a title, and if it is not `NAME(SECTION)`; a *second* `# X` further down is not detected ✗ |
| 2 | The title's name matches the file's stem and its section matches the file's section suffix, both case-insensitively. | ✓ |
| 3 | The first `##` is `NAME`, and its body is one paragraph in §6's shape: comma-separated names, ` - `, a description. (That the page's own name is among them is a warning — see §6.) | ✓ |
| 4 | `## DESCRIPTION` is present. | ✓ |
| 5 | Heading levels never skip, and there is no `####`. | ✗ — the check exists and cannot fire; `####` is handled by §6's mechanism instead |
| 6 | No `man2umm: FIXME` marker survives anywhere. | ✓ |
| 7 | Inline delimiters balance: no unescaped stray `*` or `` ` ``. | ✗ — undetectable after parsing, since an unbalanced delimiter silently becomes text (rule 2) |
| 8 | No tab outside a fenced block. | ✓ |
| 9 | No cross-reference carries inline markup. | partly — `**open(2)**` is caught, `**open**(2)` is not (§3) |
| 10 | The file ends with exactly one newline, and every heading has a blank line above and below. | ✗ — the reader discards line structure before the lint sees it |

Warnings, not errors, and both are convention only: a `##` heading outside §6's vocabulary (this
fires 57 times on the real corpus and is meant to); and a `## NAME` list that does not include the
page's own name (four pages: `CTYPE`, `DIRECTORY`, `STRING`, `TERMCAP`).

Two further conventions the tooling does not check at all: a line past 100 columns outside a fenced
or line block, and a `## SEE ALSO` that is not a plain comma-separated reference list.

---

## 10. What a renderer does

Normative for anything that formats this dialect, so that two renderers agree.
[`cmd/manview`](../cmd/manview/) is this tree's, it is on the image as `/usr/bin/manview`, and it is
the authority: this section describes what it does.

```
manview [ -p ] [ -m ] [ -w cols ] [ file ... ]
```

### 10.1 Fill, the measure, and the word

**Fill a paragraph and nothing else.** A definition body, a list item and a quote are paragraphs
and are filled. A line block, a fenced block and a heading are not: their line structure is the
source's and is emitted as it stands.

- **The measure is one column narrower than the terminal.** `-w` says how wide the *terminal* is
  (80 by default, clamped to 20…512) and text is filled to one less
  ([`render.c:86`](../cmd/manview/render.c#L86)). A glyph in the last column leaves the cursor in
  the pending-wrap position, where the following newline is swallowed or doubled depending on the
  terminal, and the fold shows up as a stray blank line under `more`. One column of padding
  removes the question.
- **Never hyphenate.** A manual page is full of tokens that must not break, so a word wider than
  the measure goes out over the margin in one piece rather than in two.
- **The word is the fill unit and a word may cross fonts.** `**b**\&*size*` is one word by rule 3
  and `open(2)` is one by construction, so the break decision is taken on the whole of it. This is
  why the joiner works.
- The measure binds the fill rule and the banner only. A line block, a fenced block and an
  over-long word may exceed it, as they already exceeded 80.

### 10.2 Indent, the step, and the hanging tag

**The step is five columns**, which is v7's.

| element | column |
|---|---|
| `## SECTION` heading | 0 |
| `### Subsection` heading | 3 |
| body text at the top level | 5 |
| a definition body, list item body or quote | 5 more than its container |

A definition's tag, a bullet's `-` and an ordered item's `1.` are set at the container's own
column and the body hangs five in from there:

```
**-l**
: List in long format, giving mode, number of links, owner, size in bytes,
  and time of last modification for each file.
```

becomes

```
     -l   List in long format, giving mode, number of links, owner, size in
          bytes, and time of last modification for each file.
```

**A tag that leaves room keeps the body on its own line; a tag that does not gets the next line**
([`render.c:277`](../cmd/manview/render.c#L277)). "Leaves room" means the tag ends *before* the
body column — at most four displayed characters for the five-column step. This is the shape v7's
`.TP` has always had.

### 10.3 The banner

The level-1 title of §5 is not rendered as text. It becomes a v7 page banner
([`render.c:369`](../cmd/manview/render.c#L369)): the page's own name at both margins with
`UNIX Programmer's Manual` centred between them.

```
LS(1)                      UNIX Programmer's Manual                       LS(1)
```

- The centred form is used when `2 × len(name) + 26 ≤ measure`. A name too wide for that gets the
  left field alone and nothing else.
- The `[extra]` qualifier goes on a **centred second line**, in roman.
- **There is no footer.** `more(1)` does the paging here and there is no page number to put in one.

### 10.4 A literal run is quoted

`` `x` `` in the source renders as `` `x' `` — v7's own quotation, a backquote and an apostrophe.
**The quote characters are the renderer's, not the source's**: the source holds only the
delimiters, which is why §2 makes this one construct rather than two punctuation characters. The
quotes are set in roman; they are punctuation the renderer added, not part of what is quoted. An
HTML back end would emit `<code>` and no quotes at all.

### 10.5 A cross-reference

**The name carries the font and the parentheses stay roman**
([`parse.c:421`](../cmd/manview/parse.c#L421)) — the traditional italic name with roman parentheses
that v7's pages had, plus a link in HTML and a colour of its own where colour is available.

### 10.6 Tabs in a fenced block

A tab advances to the next eight-column stop **counted from the block's own column 0, not from the
page's** ([`render.c:319`](../cmd/manview/render.c#L319)). The indent step is 5 and a stop is 8, so
measuring from the page shifts every column of a hand-aligned table by a different amount;
measuring from the block shifts the whole of it by five and the alignment survives, which is the
whole reason the construct exists.

An **empty line inside a fence carries no indent** — five spaces and a newline is trailing
whitespace, and a page full of it is a page nothing can `cmp` cleanly.

### 10.7 A column is a character

Bytes `0200`–`0277` are UTF-8 continuations and take no column of their own
([`render.c:96`](../cmd/manview/render.c#L96)). Without that the nine pages with Cyrillic in them
fold at half the width. Emitting still walks bytes, which is correct: it emits, it does not
measure.

### 10.8 Attributes

**On a terminal, bold and italic are ANSI SGR attributes, and colour is allowed and wanted.**

| element | attribute | colour | sequence |
|---|---|---|---|
| banner, `## HEADING` | bold | yellow | `ESC[1;33m` |
| `### Subheading` | bold | cyan | `ESC[1;36m` |
| `**bold**` — what you type | bold | green | `ESC[1;32m` |
| `*italic*` — what you replace | italic | cyan | `ESC[3;36m` |
| `` `literal` `` — quoted as itself | underline | — | `ESC[4m` |
| `name(N)` — the name only | italic | magenta | `ESC[3;35m` |

**Attributes are opened and closed around every run** rather than tracked across the output. It
costs a few bytes on a line that alternates fonts — and a word of a multi-word bold run is its own
run, since a word is the fill unit — and it buys the property that matters when the reader is
`more`: no escape sequence outlives the run it belongs to, so nothing can leave a terminal in an
attribute, and the output cut at any line boundary leaves both halves well formed.

Three flags control the output:

| flag | effect |
|---|---|
| `-p` | plain: no escape sequence of any kind. The fallback, and legible in a file. Quotation marks and the parentheses of a cross-reference are punctuation and stay. |
| `-m` | monochrome: the three attributes, no colour, for a terminal that has one and not the other. |
| `-w cols` | the width of the terminal, 80 by default, clamped to 20…512. |

**Attributes are on by default and are not conditioned on `isatty(2)`.** `man` builds
`manview page | /bin/more` and hands it to `system(3)`, so the formatter's standard output is a
**pipe** exactly when a human is reading; an `isatty()` test would suppress colour in the one case
it is wanted and light it up in none. `-p` is how a script asks for clean bytes, and `man - page`
shows the page source with no formatter at all.

### 10.9 Limits, and how they degrade

The renderer holds one *group* (§4) at a time and never a page, which is what lets a manual of any
size be read in 28,672 words of address space. The ceilings, and what each does when reached:

| limit | value | what happens past it |
|---|---|---|
| group buffer | 8,192 bytes | the group is rendered as far as it was read, the offending line opens the next group, `manview: <path>: block too long, output truncated` goes to standard error, exit status 1 |
| lines per group | 128 | as above |
| source line | 2,048 bytes | the line is split; the corpus's longest is 945 |
| nesting depth | 8 containers | the block is emitted verbatim rather than formatted, which is what the dialect promises for anything it cannot parse |
| word | 512 bytes, 32 font changes | the word is flushed early; hyphenating is refused and losing it would be worse |
| title | 128 bytes | truncated |

**Every one of these degrades rather than truncating.** The group message says "output truncated"
and no text is in fact lost: the block is split and the rest of the page formats normally, so an
over-long group costs a spurious paragraph break and an exit status, not content. A single line
always fits an empty group, so the reader cannot spin.

The largest group anywhere in this manual is 4,053 bytes and 71 lines
([`lib/libcurses/curses.3.umm`](../lib/libcurses/curses.3.umm)) and the deepest nesting is one
container inside another, so every ceiling is at least twice what the corpus has ever asked for.

### 10.10 What is deliberately absent

- **No overstrike back end.** §10.8's ANSI is the only one. Backspace overstrike — `c\bc` for bold,
  `_\bc` for italic — is `nroff`'s convention, and [`cmd/col/col.c`](../cmd/col/col.c) exists to
  strip it. This system has no `nroff`: nothing here produces overstrike and nothing but `col`
  would read it, while `cmd/more` and `cmd/novi` already write hard-coded ANSI. `-p` is the
  fallback and it is a better one, being legible in a file. A renderer may emit overstrike; none
  does.
- **No HTML back end**, no pager of its own, and no way to ask the terminal its width — there is no
  `TIOCGWINSZ` in this kernel, which is why `-w` exists.

### 10.11 Where the two readers differ

[`ummread.cpp`](../cmd/man2umm/ummread.cpp) builds a document model instead of rendering, and the
two disagree in exactly two places. `manview` is the authority in both:

1. The model tags a cross-reference's parentheses `Font::Xref` along with its name; `manview`
   leaves them roman (§10.5), which is what v7's pages looked like.
2. The model post-processes cross-references over finished spans, so `*malloc*(2)` becomes one
   reference span there; `manview` scans as it reads and renders it as an italic word followed by
   roman parentheses. §3 forbids writing one either way.

---

## 11. The roff correspondence

The complete disposition of every `-man` request the corpus used, for anyone reading a page's
history or converting one by hand. Counts are from the 200 pages as they stood before conversion.

| request | n | becomes |
|---|---|---|
| `.TH` | 200 | the level-1 title, §5 |
| `.SH` | 1271 | `##` |
| `.SS` | 114 | `###` |
| `.PP` `.LP` | 1455 | a paragraph break |
| `.B` | 2469 | `**…**` |
| `.I` | 2540 | `*…*` |
| `.BR` `.RB` `.IR` `.RI` `.BI` `.IB` | 1563 | alternating runs, **joined with no space** — roff's own rule |
| `.TP` | 555 | a definition list |
| `.IP tag` | 94 | a definition list |
| `.IP 1.` | 11 | an ordered list |
| `.IP \(bu` | 9 | a bullet list |
| `.IP` `.IP ""` | 52 | a quote — an indented display, not a definition |
| `.HP` | 28 | rewritten by hand as a definition list |
| `.br` | 333 | the paragraph becomes a line block |
| `.nf` `.fi` | 87 | a fenced block, or a line block where the region has fonts in it |
| `.RS` `.RE` | 72 | a quote |
| `.\"` | 236 | `<!-- … -->` |
| `` `x' `` | 330 | a literal run, `` `x` `` — the quote characters are the renderer's (§2) |
| `.de` `..` `.ds` | 11 | expanded at conversion time; **the dialect has no macros** |
| `.so` | 7 | the named header inlined as a fenced `c` block |
| `.ns` `.PD` | 114 | dropped — compact-list typography, and there is one list form (§4) |
| `.SM` | 44 | dropped — `nroff` rendered it roman anyway |
| `.ta` | 13 | dropped — a fenced block preserves columns by construction |
| `.if` | 12 | the `n` branch is taken, the `t` branch discarded: the target is a terminal |
| `.UC` | 2 | dropped — the provenance survives as a comment |
| `.DT` `.ne` `.dt` `.tr` `.nh` `.hy` `.ft` `.ce` `.ad` `.pg` `.ti` `.in` | 22 | dropped |

And the escapes:

| escape | n | becomes |
|---|---|---|
| `\-` | 1640 | `-` |
| `\fB` `\fI` `\fR` `\fP` | 597 | a run boundary; `\fR` and `\fP` both close |
| `\"` | 236 | the rest of the line is a comment |
| `\|` `\^` | 176 | nothing — the neighbours join |
| `\*x` `\*(xx` | 111 | the page's own `.ds` text, substituted |
| `\e` | 50 | `\\`, or a bare `\` inside a fenced block |
| `\ ` | 41 | one space |
| `\w'…'u` `\h` | 25 | dropped — width padding a filled renderer does not need |
| `\'` `` \` `` | 39 | `'` and `` ` `` — and where the pair quotes a word, a literal run |
| `\s±N` | 16 | dropped — a terminal has one type size |
| `\&` | 10 | kept: it is this dialect's joiner too |
| `\u` `\d` | 6 | `^`, §8 |
| `\z` `\v` `\c` | 6 | rewritten by hand |

**The dialect has no macro facility, and must not grow one.** `.ds` and `.de` were expanded at
conversion time and discarded. A page that wants a repeated phrase types it.

---

## 12. Converting a page

A v7 program ported into this tree arrives with a roff page.
[`cmd/man2umm`](../cmd/man2umm/) converts it and is kept for exactly that purpose:

```sh
b6man2umm -o cmd/foo/foo.1.umm cmd/foo/foo.1
b6man2umm -l cmd/foo/foo.1.umm
```

Then verify against the host's own formatter before deleting the roff —
[`scripts/mancheck.py`](../scripts/mancheck.py) compares `groff -man`'s rendering with the
Markdown's along three axes: the word stream, the section structure, and the font of every
character.

```sh
python3 scripts/mancheck.py cmd/foo/foo.1 cmd/foo/foo.1.umm
```

And look at the result, which is the check this document is ultimately for:

```sh
b6sim build/rootfs/usr/bin/manview -p cmd/foo/foo.1.umm
```

The converter is mechanical and its output is a draft. Read it. It will have dropped a `.ta`, or
turned a hand-built table into a filled paragraph, or left an `.HP` as a line block that wanted to be
a definition list. [`cmd/man2umm/README.md`](../cmd/man2umm/README.md) lists what needs a human.

Beyond the mechanics, [`cmd/README.md`](../cmd/README.md) §10 is the rule about *content*: correct
the page in place, give it an ANSI SYNOPSIS, fix every claim this machine falsified and mark it
`Note:`, report blocks in 1024 bytes, and write `DIRSIZ` as 18 where it shows.
