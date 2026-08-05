# Unix Manual Markdown

The format this system's manual pages are written in. It is a **small semantic dialect of
Markdown**, designed for reference material rather than prose, and it replaces the roff `-man`
macros the pages were inherited in.

A page is a plain text file named `<name>.<section>.md` and living beside the source it documents —
[`cmd/ls/ls.1.umm`](../cmd/ls/ls.1.umm), [`lib/libc/man/read.2.umm`](../lib/libc/man/read.2.umm),
[`include/man/dir.5.umm`](../include/man/dir.5.umm). That placement is
[`cmd/README.md`](../cmd/README.md) §10's rule and it does not change.

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

Contents:

- [1. Three rules that govern everything](#1-three-rules-that-govern-everything)
- [2. Inline markup](#2-inline-markup)
- [3. Cross-references](#3-cross-references)
- [4. Block constructs](#4-block-constructs)
- [5. The page title](#5-the-page-title)
- [6. Sections](#6-sections)
- [7. SYNOPSIS](#7-synopsis)
- [8. Characters and escapes](#8-characters-and-escapes)
- [9. Canonical shape](#9-canonical-shape)
- [10. What a renderer is expected to do](#10-what-a-renderer-is-expected-to-do)
- [11. The roff correspondence](#11-the-roff-correspondence)
- [12. Converting a page](#12-converting-a-page)

---

## 1. Three rules that govern everything

**Rule 1. `_` is never a delimiter.** Italic is `*x*`, and only `*x*`. A manual page is full of
`time_t`, `d_ino`, `ac_comm[10]`, `SIG_DFL`, `_exit`, `__SYMDEF`; if underscore opened emphasis,
every one of them would need escaping. It does not, so none of them do.

**Rule 2. A delimiter must be flanked by a non-alphanumeric.** An opening `*`, `**` or `` ` `` counts
as one only if the character before it is the start of the line or a non-alphanumeric; a closing one
counts only if the character after it is the end of the line or a non-alphanumeric. Two consequences,
both wanted:

```
2**41-1            the ** follows a digit -- literal, no escape needed
*malloc*(2)        the closing * precedes '(' -- closes correctly
(**-t**)           opens after '(' and closes before ')'
```

**Rule 3. `\&` is the zero-width joiner.** Borrowed from roff, where it means the same thing. It
butts two marked runs together with no space between them:

```
**b**\&*size*      renders as   b size   with no gap: bsize
```

It is written only where it is needed — between two adjacent marked runs. Where one side is
unmarked, rule 2 has already made the boundary unambiguous.

---

## 2. Inline markup

Four span kinds, and there are no others. No links, no images, no raw HTML, no `~~strike~~`, no
`_em_`, no `***both***`.

| span | written | means |
|---|---|---|
| roman | `text` | ordinary prose |
| bold | `**text**` | type this literally: a command, a flag, a keyword, a file name in running text |
| italic | `*text*` | replace this: an argument, a variable, a metasyntactic name; also a first mention |
| literal | `` `text` `` | a character or short token **quoted as itself** — v7's `` `text' `` |

The distinction between **bold** and *italic* is the one a `-man` page has always drawn in a
SYNOPSIS and it carries meaning, not weight: bold is what the user types, italic is what the user
substitutes. A renderer may colour the two differently, and should.

A span may not span a line break inside a paragraph — close it and reopen it, or use a line block
(§4) where the break is intentional.

**A literal run is where v7's quoting went.** v7 wrote a backquote, the text and an apostrophe —
`` `environment' ``, `` `cat a b >a' ``, `` `-' `` — to mean *this, as itself*, which is exactly
what a literal run means. So it is one construct here rather than two punctuation characters, and
a renderer supplies the quotes (§10). The corpus had 330 of them; recognizing the construct rather
than transliterating it is what keeps `` \` `` out of the prose.

---

## 3. Cross-references

**A cross-reference is written with no markup at all**, as `name(N)`:

```
See open(2) and the discussion in ls(1).
malloc(3), free(3), realloc(3)
```

Recognized by the grammar `[A-Za-z_][A-Za-z0-9_.+-]*` immediately followed by `(`, a section digit
`1`–`8`, an optional lowercase letter, and `)`. No space anywhere in it.

This is the dialect's single most important extension. Roff spelled the same thing
`.IR open (2),` — two macros and a hard line break for a construct that appears about twelve hundred
times across the corpus. Here it is a plain token that reads correctly with no renderer at all.

**A renderer supplies the presentation**: the traditional italic name with roman parentheses on a
terminal, a link in HTML, and a colour of its own where colour is available. A cross-reference is a
distinct span kind in the document model for exactly that reason.

**A cross-reference must not carry inline markup.** Write `open(2)`, never `*open*(2)` or
`**open**(2)`. The lint enforces this, because a marked-up reference cannot be recognized and would
lose its link.

Where a page means the literal text and not a reference — a C function type, say — break the
adjacency: `` `printf(3)` `` inside a literal run is not a reference.

---

## 4. Block constructs

Blocks are separated by blank lines. Nine kinds.

### Paragraph

Ordinary filled text. Line breaks inside a paragraph are not significant; a renderer refills to the
output width. Source lines should be wrapped near 95 columns.

### Line block

Lines beginning `| `. Each source line becomes exactly one output line, and inline markup is live
inside it.

```
| **cat** [ **-u** ] *file* ...
| **ls** [ **-1ACFRacdfgilqrstu** ] *name* ...
```

This is the construct for anything that is both **formatted and line-broken**: a command synopsis, a
short table with emphasis in it, a run of related declarations. It is the only construct that
carries fonts *and* hard line breaks; a fenced block carries breaks but no fonts, and a paragraph
carries fonts but no breaks.

Leading spaces after the `| ` are preserved, so a line block can be used for light column work.

### Fenced block

Verbatim text between ` ``` ` fences. **No markup is interpreted inside**; every character is
literal, and tabs are permitted here and nowhere else. An info string names the language when there
is one:

````
```c
struct direct {
    ino_t d_ino;
    char  d_name[DIRSIZ];
};
```
````

Use `c` for C declarations, no info string for shell transcripts, output samples and tables. Fenced
blocks are the right home for anything column-aligned, since the columns survive by construction.

### Definition list

A term on its own line, then one or more definitions each introduced by `: ` in the first column of
the block. Continuation lines are indented two spaces.

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

This is the workhorse. Options, error numbers, `FILES` entries, structure members and the tagged
paragraphs of a `DESCRIPTION` are all definition lists.

### Bullet list and ordered list

```
- the first thing
- the second thing

1. the first step
2. the second step
```

Continuations indent two spaces (three for an ordered list past item 9). Lists nest by indentation.

### Quote

Lines beginning `> `. An indented display that is not a definition — a worked example, a short
transcript, an aside set apart from the running text.

```
> cat file1 file2 >file3
```

Quotes may contain any other block, indented under the `> `.

### Comment

```
<!-- UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. -->
```

Not rendered. Provenance notes live here, at the top of the file, above the title.

### Heading

`##` for a section, `###` for a subsection. See §6.

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
- **SECTION** is a digit `1`–`8` with an optional uppercase letter: `1`, `2`, `3`, `1M`, `3S`, `3X`,
  `3M`, `5`, `8`. It must match the file's section suffix.
- **`[extra]`** is an optional free-text qualifier in square brackets, for the handful of pages that
  carry one: `# GETPW(3) [deprecated]`, `# LOCK(2) [local]`, `# PHYS(2) [PDP11]`,
  `# CURSES(3) [April 23, 1986]`.

There is no front matter. Five pages out of two hundred want a fourth field and a bracket serves
them; a YAML block at the head of every page to serve five is a poor trade.

The section letters mean what they have always meant here: `1M` a maintenance command, `3S` the
standard I/O library, `3M` the mathematical library, `3X` a routine that is in neither, `5` a file
format, `8` a program run by the system rather than by a user.

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

`BESM-6 NOTES` is the house convention and it is load-bearing — sixty-six pages carry one. It is
where a divergence from v7 is recorded at the point a reader will look for it. A page may also
invent a prose heading for something that deserves its own place: `## HOW MANY FILES`,
`## BUFFERING`, `## BLOCKS ARE 1024 BYTES`. That freedom is deliberate and the lint only warns.

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

Heading levels may not skip, and there is no `####`.

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
itself. Roff needed `\(mu` and a `-Kutf8` flag; nothing here does.

Only five characters are ever escaped, and only where rule 2 would otherwise make them markup:

| escape | for |
|---|---|
| `\*` | a literal asterisk that would open or close italic |
| `` \` `` | a literal backtick |
| `\\` | a literal backslash |
| `\&` | the zero-width joiner of rule 3 — it is not a literal `&` |
| `\|` | a literal pipe at the very start of a line, where it would begin a line block |

`#`, `>`, `-`, `:`, `1.` need escaping **only in column 1** of a block, where they would start a
different block. Mid-line they are ordinary text.

`_`, `[`, `]`, `<`, `>`, `~`, `&`, `(`, `)` are **never** escaped. This is what keeps a raw page
readable.

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

A page must satisfy these. `b6man2umm -l` checks them and runs as a `ctest` over every page in the
tree.

1. Zero or more comment lines, then **exactly one** level-1 heading matching §5's grammar, and no
   other level-1 heading anywhere in the file.
2. The title's name matches the file's stem and its section matches the file's section suffix, both
   case-insensitively.
3. The first `##` is `NAME`, and its body is one paragraph in §6's shape: comma-separated names, ` - `,
   a description. (That the page's own name is among them is a warning, not an error — see §6.)
4. `## DESCRIPTION` is present.
5. Heading levels never skip, and there is no `####`.
6. No `man2umm: FIXME` marker survives anywhere.
7. Inline delimiters balance: no unescaped stray `*` or `` ` ``.
8. No tab outside a fenced block.
9. No cross-reference carries inline markup.
10. The file ends with exactly one newline, and every heading has a blank line above and below.

Warnings, not errors: a line past 100 columns outside a fenced or line block; a `## SEE ALSO` that is
not a plain comma-separated reference list; a `##` heading outside the conventional vocabulary.

---

## 10. What a renderer is expected to do

Normative for anything that formats this dialect, so that two renderers agree.

- **Fill paragraphs** to the output width; do not fill a line block, a fenced block or a heading.
- **Indent** a definition body, a list item and a quote by a fixed step (5 columns is v7's).
- **Cross-references** get the name in italic and the parentheses in roman, which is what v7's pages
  did, plus a link in HTML and a colour of their own where colour is available.
- **On a terminal, bold and italic are backspace overstrike** — `c\bc` for bold, `_\bc` for italic —
  which is what `nroff` emitted and what [`cmd/col/col.c`](../cmd/col/col.c) exists to strip. A
  renderer targeting a terminal that understands ANSI may emit SGR attributes instead, and may add
  colour, but overstrike is the default and the fallback.
- **A literal run is quoted on a terminal** — `` `x' `` as v7 rendered it, or the terminal's own
  quotation marks — and is `<code>` in HTML. The quote characters are the renderer's, not the
  source's: the source has only the delimiters.
- **Never hyphenate.** A manual page is full of tokens that must not break.

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
| `.ns` `.PD` | 114 | dropped — compact-list typography, and there is one list form |
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
[`scripts/mancheck.sh`](../scripts/mancheck.sh) compares `groff -man`'s rendering with the
Markdown's along three axes: the word stream, the section structure, and the font of every
character.

```sh
sh scripts/mancheck.sh cmd/foo/foo.1 cmd/foo/foo.1.umm
```

The converter is mechanical and its output is a draft. Read it. It will have dropped a `.ta`, or
turned a hand-built table into a filled paragraph, or left an `.HP` as a line block that wanted to be
a definition list. [`cmd/man2umm/README.md`](../cmd/man2umm/README.md) lists what needs a human.

Beyond the mechanics, [`cmd/README.md`](../cmd/README.md) §10 is the rule about *content*: correct
the page in place, give it an ANSI SYNOPSIS, fix every claim this machine falsified and mark it
`Note:`, report blocks in 1024 bytes, and write `DIRSIZ` as 18 where it shows.
