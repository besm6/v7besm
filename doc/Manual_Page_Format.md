# Unix Manual Markdown

A **small semantic dialect of Markdown** for reference material, replacing the roff `-man`
macros manual pages were traditionally written in. This document is the specification: it is
complete enough to write a conforming reader and renderer from.

A page is a plain text file holding one manual entry. It carries a **name** and a **section** —
a digit `1`–`8` with an optional subsection letter — and the conventional file name joins the
two: `ls.1`, `fsck.1m`, `stdio.3s`.

Why not roff: a `-man` page is legible only through a formatter. Why not plain CommonMark: it
has no definition lists, which is what four out of five `-man` constructs are; it makes `_` a
delimiter, intolerable in a document made of `time_t` and `SIG_DFL`; and it cannot say "this
run is an argument you replace" as distinct from "this run is bold".

**It is not a Markdown implementation.** No link or image syntax, no raw HTML, no setext
heading, no thematic break, no `~~strike~~`, no `_em_`, no `+ ` or `* ` bullet, no pipe table,
no macros. A construct this dialect does not recognize is **text** — not an extension and not
an error.

---

## 1. The rules that govern everything

Five rules; §§2–4 are their consequences.

### Rule 1. `_` is never a delimiter

Italic is `*x*`, and only `*x*`, so `time_t`, `SIG_DFL` and `_exit` need no escaping. An
underscore is still an ordinary character, and rule 2 counts it as a non-alphanumeric — `_*u*_`
sets `u` in italic between two literal underscores.

### Rule 2. A delimiter is flanked on the outside and closed up on the inside

This governs `*` and `**`, and nothing else; a literal run has its own rule (§2).

An asterisk run **opens** a marked run only if the character *before* it is the start of the
line or is not a letter or digit, **and** the character *after* it exists and is not a space or
tab. It **closes** the run it is inside only if the character *before* it is not a space or
tab, **and** the character *after* it is the end of the line or is not a letter or digit. In
one sentence: **the outside of a delimiter must not be alphanumeric, the inside must not be
blank.** An asterisk that neither opens nor closes is an ordinary character, needing no escape.

| written | what happens | why |
|---|---|---|
| `2**41-1`, `a*b*c` | text | an alphanumeric is on the outside |
| `*malloc*(2)`, `(**-t**)` | italic `malloc`, bold `-t` | punctuation outside, text inside |
| `_*u*_` | italic `u` | `_` is not alphanumeric |
| `* x*` | text | a space is *inside* the opener |
| `*x *`, `*ab*c` | do **not** close at the second `*` | a space inside, a letter outside |

The last two are why an unbalanced page fails quietly: a delimiter that fails its test is
consumed as text and the marked run runs to the end of the block. Write balanced runs (§9.7).

### Rule 3. `\&` is the zero-width joiner

Borrowed from roff. It butts two marked runs together with no space between them and produces
no character of its own: `**b**\&*size*` renders as bold `b` against italic `size` with no gap,
`*name*\&'s` as *name*'s.

**It is the form that always works.** Butting delimiters together comes out right in one
direction — `**b***size*` also gives `bsize` — and wrong in the other: `*a***b**` leaves italic
open and swallows the block, because rule 2's closing test fails against the following `b`.

Write it only between two adjacent marked runs, or between a marked run and an alphanumeric
that must touch it; elsewhere rule 2 has already made the boundary unambiguous. It works
because a *word* is the fill unit and may cross fonts (§10.1).

### Rule 4. Markup does not nest

There is one font at a time. Inside a bold or italic run a backtick is an ordinary character
and an asterisk opens nothing; inside a literal run *nothing* is markup:

```
**bold with *stars* inside**      the stars print
*italic with `tick` inside*       the backticks print
***both***                        not a construct
`open(2)`                         not a cross-reference (§3)
```

A page wanting bold *and* a quoted token writes two runs joined by rule 3, or types the
quotation marks by hand.

### Rule 5. A paragraph is one text, and its source lines are joined before it is read

The lines of a paragraph are concatenated with a single space between them, and only then is
the result scanned for markup, so **a marked run may cross a source line** — a literal run may
open on one line and close on the next.

The corollary is the limit: **a heading and a line block are one line each**, so a run cannot
cross a line there. Nor can a run cross a *block* boundary — a blank line, or the start of the
next construct, ends every open font.

---

## 2. Inline markup

Four span kinds, and no others.

| span | written | means |
|---|---|---|
| roman | `text` | ordinary prose |
| bold | `**text**` | type this literally: a command, a flag, a keyword, a file name in running text |
| italic | `*text*` | replace this: an argument, a variable, a metasyntactic name; also a first mention |
| literal | `` `text` `` | a character or short token **quoted as itself** |

Bold against italic is the distinction a `-man` SYNOPSIS has always drawn, and it carries
meaning rather than weight: **bold is what the user types, italic is what the user
substitutes.** A cross-reference (§3) is a fifth kind in the document model and in a renderer's
font table, but it is written with no markup, so it is not a span an author opens and closes.

**The literal run** is where traditional Unix quoting went: a `-man` page wrote a backquote,
the text and an apostrophe — `` `environment' `` — to mean *this, as itself*. It is one
construct rather than two punctuation characters, and **the renderer supplies the quotation
marks** (§10.4); the source holds only the delimiters.

A backtick opens a run **wherever it stands in roman text**, and it closes at the next backtick
in the same block. Rule 2's flanking test does not apply — `` a`b`c `` is a literal `b` between
two letters — because a backtick has no other meaning in prose. Three properties follow, all
load-bearing:

1. **The body is raw**: no escape processed, no markup recognized, no cross-reference found
   inside it. That is what lets a page write `` `\n` `` with the backslash intact, and why
   `` `printf(3)` `` is not a reference.
2. **A backtick inside a literal run needs the padded form**: doubled delimiters and one space
   of padding on each side, of which exactly one leading and one trailing space is stripped, so
   `` `` a`b `` `` renders as `` `a`b' ``.
3. **A run that is never closed is not a run.** The backtick stands as an ordinary character.

---

## 3. Cross-references

**A cross-reference is written with no markup at all**, as `name(N)` — `See open(2) and the
discussion in ls(1).` Roff spelled the same thing `.IR open (2),`; here it is a plain token
that reads correctly with no renderer at all.

```
name  := [A-Za-z_] [A-Za-z0-9_.+-]*
xref  := name "(" [1-8] [a-z]? ")"
```

No space anywhere in it. Two further conditions decide whether a match is taken:

- **The name must start at a word boundary** — the character before it is neither alphanumeric
  nor `_`. Without this, `sprintf(3)` would match at `printf` and lose two letters.
- **The scan is in roman text only**, as the text is read, so a `name(N)` inside a bold,
  italic or literal run is not one.

So `open(2)`, `sprintf(3)`, `fsck(1m)`, `_exit(2)`, `x.y(2)`, `a_b(3s)` and `a+b(1)` are
references — a name may begin with `_`, and `.` `_` `+` `-` are name characters. `FSCK(1M)` is
not (the letter must be lowercase), nor `open(9)` (the digit is `1`–`8`), nor `open (2)` or
`open(2` (no space, and it must close), nor `9open(2)` (no leading digit), nor
`` `printf(3)` `` or `**open(2)**` (not roman text).

**The letter's case is not a title's.** A reference is lowercase, `fsck(1m)`; the level-1 title
is uppercase, `# FSCK(1M)`; the file is `fsck.1m`. Where a page means the literal text rather
than a reference — a C function type, a shell word — put it in a literal run.

**A cross-reference must not carry inline markup.** Write `open(2)`, never `*open*(2)` or
`**open**(2)`: a marked-up reference cannot be recognized and loses its link, rendering as an
italic word followed by roman parentheses, which *looks* almost right and is not a reference.

---

## 4. Block constructs

Nine kinds, listed in the order a reader must test for them: the first match wins.

**The group.** A reader works on one at a time — a run of lines delimited by a blank line *that
is followed by a line at column 0*, fenced blocks excepted; a blank line followed by an
*indented* line ends nothing and stays inside the group. A conforming reader can therefore hold
one group and never a page. This is also what lets a definition body hold more than one
paragraph: a body is whatever is indented under its marker, blank lines and all, ending at the
first line that is neither indented nor preceded by more indented text — which stops a
definition swallowing the next `## SECTION`. **Indentation is spaces**; a tab in column 1 is
not indentation and continues nothing.

**Blank lines set the spacing, not the structure.** A block needs no blank line before it: a
`- `, `: `, `> `, `| `, `N. `, `## `, a fence or a comment line ends the paragraph above it
where it stands, and so does the *term* line of a definition list. A blank line instead sets
vertical spacing — **two sibling blocks written with nothing between them are set with nothing
between them; two written a blank line apart get a blank line** — which is the only thing that
can tell a compact list from a spaced one, since the dialect has one list form.

| kind | trigger, at the block's own column 0 | how far it runs | continuation indent |
|---|---|---|---|
| fenced block | three backticks | to the next line beginning with three backticks | — |
| comment | `<!--` and a space | that one line | — |
| heading | `## ` or `### ` | that one line | — |
| line block | `\| ` or a line that is exactly `\|` | the maximal run of such lines | — |
| quote | `> ` or a line that is exactly `>` | the maximal run of such lines | 2 (the marker) |
| bullet list | `- ` | that line and everything indented under it | 2 |
| ordered list | one or more digits, `.`, one space | that line and everything indented under it | the label's width: 3 for `1. `, 4 for `10. ` |
| definition list | any line whose **next** line begins `: ` | the `: ` line and everything indented under it | 2 |
| paragraph | anything else | to a blank line, to a line that starts a block, or to the line before a definition term | — |

The level-1 title `# ` is recognized in the same dispatch but is not a block in this sense (§5).

**Paragraph.** Filled text; line breaks are not significant, since the lines are joined with
one space by rule 5 and refilled to the output width. Wrap source lines near 95 columns. A
paragraph always consumes at least its first line, so an unrecognized construct degrades to
text.

**Line block.** Lines beginning `| `, or lines that are exactly `|`; each becomes exactly one
output line, with inline markup live inside it.

```
| **ls** [ **-1ACFRacdfgilqrstu** ] *name* ...
```

This is the construct for anything both **formatted and line-broken**: a command synopsis, a
short table with emphasis, a run of declarations. It alone carries fonts *and* hard line
breaks; a fenced block carries breaks but no fonts, a paragraph fonts but no breaks. Leading
spaces after the `| ` are preserved; a bare `|` is an **empty line** inside the block, not an
empty block; `|x`, with no space, is a paragraph; and rule 5 does not apply, each line being
scanned on its own.

**Fenced block.** Verbatim text between three-backtick fences. **No markup is interpreted
inside**; every character is literal, and **tabs are permitted here and nowhere else**. An info
string may name the language — `c` for C declarations, none for shell transcripts and output
samples — but **it is advisory**: a terminal back end may discard it, one with fonts may use
it. A fence opens and closes wherever it stands, at column 0 or indented under a definition;
the closing fence is the next line beginning with three backticks, and an unterminated fence
runs to the end of the group without being an error. Nothing inside a fence ends a group, so it
may hold blank lines and lines at column 0, and it is the home for anything column-aligned,
whose columns survive by construction (§10.6).

**Definition list.** A term on its own line, then one or more definitions each introduced by
`: `. Continuation lines indent two spaces; a definition may hold several paragraphs and nested
blocks.

```
**-l**
: List in long format, giving mode, number of links, owner, size in bytes,
  and time of last modification for each file.

**-u**
: Do not buffer the output.

  Buffering is otherwise in filesystem blocks, and is bypassed anyway when
  the standard output is a terminal.
```

**The term and its `: ` must be adjacent** — no blank line between them, and the term line must
not itself begin a block, since a term is recognized only by what the *next* line is. **A `: `
line with no term above it is still a definition**, with an empty tag: that is how a second
definition under one term, or a bare indented body, is written. This is the workhorse — options,
error numbers, `FILES` entries, structure members and tagged paragraphs are all definition
lists. §10.2 has how the tag is set.

**Bullet list and ordered list.** `- ` and, for the ordered form, one or more digits, a period
and one space; `1.` at the end of a line is not one. **The continuation indent is the marker's
own width**: two under `- `, three under `1. ` through `9. `, four under `10. ` and beyond.
Lists nest by indenting a whole item that far.

**Quote.** Lines beginning `> `, or lines that are exactly `>` — an indented display that is
not a definition: a worked example, a short transcript, an aside. The marker is on *every*
line, not just the first, and two columns are stripped from each. A quote may contain any other
block, indented under the `> `.

**Comment.** `<!-- Provenance note, above the title. -->`, not rendered. The trigger is `<!--`
followed by a space, exactly; `<!--x` is a paragraph. **A comment is one line** — there is no
multi-line form and a second line would render as text, so a page needing several writes one
per line.

**Heading.** `##` for a section, `###` for a subsection, each one line, each with inline markup
live inside it (§6).

---

## 5. The page title

Exactly one level-1 heading, the first thing in the file after any comments:

```
# NAME(SECTION)
# NAME(SECTION) [extra]
```

- **NAME** is the page's principal name, upper case by convention, matching the file's stem.
- **SECTION** is a digit `1`–`8` with an optional **uppercase** letter: `1`, `1M`, `3S`, `3M`,
  `3X`, `5`, `8`. It must match the file's section suffix, which is lowercase — `# FSCK(1M)`
  lives in `fsck.1m`, and a cross-reference to it is lowercase again (§3). The letters have
  their traditional meanings: `1M` a maintenance command, `3S` the standard I/O library, `3M`
  the mathematical library, `3X` a routine in neither, `5` a file format, `8` a program run by
  the system rather than by a user.
- **`[extra]`** is an optional free-text qualifier in square brackets — `# GETPW(3)
  [deprecated]`, `# MAN(1) [April 19, 1988]` — split off at the first ` [` when the line also
  ends in `]`. There is no other front matter.

**The title is not rendered as text.** It becomes the page banner (§10.3).

---

## 6. Sections

`## HEADING` — upper case, verbatim, one blank line above and below. Every page has `NAME` and
`DESCRIPTION`, and `NAME` comes first; beyond that **section order is not prescribed**.

| section | content | | section | content |
|---|---|---|---|---|
| `NAME` | see below — the one fixed body | | `DIAGNOSTICS` | what it says when it fails, and its exit status |
| `SYNOPSIS` | how it is invoked or declared (§7) | | `SEE ALSO` | a comma-separated list of cross-references, nothing else |
| `DESCRIPTION` | what it does | | `BUGS` | known limitations |
| `OPTIONS` | when the flags are too many for `DESCRIPTION` | | `AUTHOR` | where one is named |
| `FILES` | the paths it reads and writes | | `ASSEMBLER` | the calling sequence from assembly |

`NOTES`, `EXAMPLES`, `LIMITS`, `ENVIRONMENT` and `EXIT STATUS` are also conventional, and a
page may invent a prose heading for something deserving its own place — `## BUFFERING`. That
freedom is deliberate.

**The NAME section** is one paragraph in one fixed shape: comma-separated names, then ` - `
(**one ASCII hyphen, one space either side**), then a one-line description with no trailing
period — `malloc, free, realloc, calloc - main memory allocator`. This is the line an index and
a `whatis` database are built from, so its shape is checked rather than merely recommended. The
page's own name should be among those listed but need not be: a page named for a *topic* lists
the members of that topic instead.

**Subsections** are `### Title`, sentence case, set three columns in against a section's zero
(§10.2).

**Inline markup is live in a `##` and a `###`** — `` ### `kill -1 1` does not bring the system
down ``. A heading is scanned exactly as a paragraph is, with one difference: **unmarked text
takes the heading's own font, and a marked run inside it keeps its own.** Rule 5 does not apply
(a heading is one line) and trailing whitespace is trimmed.

**Levels may not skip, and there is no `####`** — which need not be enforced, since `#### X`
matches no heading trigger and falls through to a paragraph beginning with four literal `#`
characters.

---

## 7. SYNOPSIS

Two shapes, decided by what the page documents.

**A C interface gets a fenced `c` block**: there are no font distinctions to make and the
declaration should be copy-pasteable. Prototypes are written as the system actually declares
them, not in a pre-ANSI untyped form.

````
```c
#include <unistd.h>

int read(int fildes, char *buffer, int nbytes);
```
````

**A command gets a line block**, because bold and italic carry the meaning:
`| **ls** [ **-1ACFRacdfgilqrstu** ] *name* ...`. The notation inside it is the traditional
one: `**-x**` type it exactly, `*file*` replace it, `[ ... ]` optional, `...` repeatable, `|`
between alternatives choose one. Square brackets and ellipses are literal characters, not
markup — there is no link syntax in this dialect, so `[` never begins one.

---

## 8. Characters and escapes

**The source is UTF-8** and needs no declaration: a `×`, an `—` or a Cyrillic comment is
written as itself, where roff needed `\(mu` and a flag. A renderer counts a *character* and not
a byte (§10.7), so such a page fills correctly.

**A backslash makes the character after it literal, and is itself dropped.** `\&` is the one
exception: it produces no character at all (rule 3). A backslash at the very end of a line is
an ordinary backslash.

**The inline set** — the only characters ever needing an escape inside running text, and only
where rule 2 or the literal-run rule would otherwise make them markup — is `\*` for an asterisk
that would open or close italic, `` \` `` for a backtick, `\\` for a backslash, and `\&` for
the joiner of rule 3, which is not a literal `&`. `_`, `[`, `]`, `<`, `>`, `~`, `&`, `(`, `)`,
`#`, `:`, `-`, `|` are **never** escaped mid-line; that is what keeps a raw page readable.

**The column-1 set** — a block character needs protecting when a *continuation* line of a
paragraph happens to start with it: `\-` (a bullet list), `\.` after digits (an ordered list:
`60\.`), `\#` (a heading), `\*` (italic, by rule 2), `\|` (a line block), `\>` (a quote), `\:`
(a definition). The cleaner fix is usually to rewrap so the character is not in column 1.

**Escapes are inert inside a fenced block and inside a literal run**, where a backslash is an
ordinary character and the body is copied out as it stands — which is what makes `` `\n` ``
render as `` `\n' ``. A page meaning a literal backslash *in prose* writes `\\`.

**The roff special characters** are written as themselves, in UTF-8:

| roff | here | | roff | here | | roff | here |
|---|---|---|---|---|---|---|---|
| `\(em` | `—` | | `\(mu` | `×` | | `\(pl` | `+` |
| `\(sc` | `§` | | `\(aa` | `´` | | `\(**` | `*` |
| `\(*p` | `π` | | `\(+-` | `±` | | `\(bv` | `\|` |
| `\(eq` | `=` | | `\(fm` | `′` | | `\(rq` `\(lq` | `"` |

Two deliberately stay ASCII. **`\(mi` is `-`**, not U+2212: it is arithmetic prose ("returns
-1"), and a typographic minus there gratuitously changes text a program's output must match.
**`\(or` is `|`**, not U+2223: it is a shell pipe or a grammar alternation, ASCII in the thing
being described. **Superscripts are `^`** — `10^9`, `2^15` — since roff's `\u`/`\d` had no
ASCII rendering at all.

---

## 9. Canonical shape

The shape a page must have. This is the **author's** contract, not the renderer's: a conforming
renderer implements none of it and will format a page that breaks every rule. A lint tool is
the right place to check it.

1. Zero or more comment lines, then **exactly one** level-1 heading matching §5's grammar, and
   no other level-1 heading anywhere in the file.
2. The title's name matches the file's stem and its section the file's section suffix, both
   case-insensitively.
3. The first `##` is `NAME`, and its body is one paragraph in §6's shape.
4. `## DESCRIPTION` is present.
5. Heading levels never skip, and there is no `####`.
6. No conversion-tool marker survives anywhere.
7. Inline delimiters balance: no unescaped stray `*` or `` ` ``. Undetectable after parsing —
   an unbalanced delimiter silently becomes text by rule 2 — so check the raw source or not at
   all.
8. No tab outside a fenced block.
9. No cross-reference carries inline markup (§3). Note that `**open**(2)` — bold name, roman
   parentheses — is invisible to a checker that only suppresses uniformly marked matches.
10. The file ends with exactly one newline, and every heading has a blank line above and below.

Worth checking as warnings: a `##` that *means* one of §6's sections but is spelled some other
way — `See Also`, `SEE-ALSO` — since a reader and an index both go by the exact string; and a
`## NAME` list omitting the page's own name. An invented prose heading is **not** one of them:
§6 grants that freedom deliberately and about fifty pages here take it, so warning on every
heading outside the vocabulary buries the near-misses it is worth catching. Two more that tooling usually cannot: a line past 100 columns outside a fenced
or line block, and a `## SEE ALSO` that is not a plain comma-separated reference list.

---

## 10. What a renderer does

Normative for anything that formats this dialect, so that two renderers agree.

### 10.1 Fill, the measure, and the word

**Fill a paragraph and nothing else.** A definition body, a list item and a quote are
paragraphs and are filled. A line block, a fenced block and a heading are not: their line
structure is the source's and is emitted as it stands.

- **The measure is one column narrower than the terminal.** A glyph in the last column leaves
  the cursor in the pending-wrap position, where the following newline is swallowed or doubled
  depending on the terminal.
- **Never hyphenate.** A manual page is full of tokens that must not break, so a word wider
  than the measure goes over the margin in one piece rather than in two.
- **The word is the fill unit and may cross fonts.** `**b**\&*size*` is one word by rule 3 and
  `open(2)` is one by construction, so the break decision is taken on the whole of it.
- The measure binds the fill rule and the banner only; a line block, a fenced block and an
  over-long word may exceed it.

### 10.2 Indent, the step, and the hanging tag

**The step is five columns.** A `## SECTION` heading sits at column 0, a `### Subsection` at 3,
top-level body text at 5, and a definition body, list item body or quote 5 more than its
container. A definition's tag, a bullet's `-` and an ordered item's `1.` are set at the
container's own column with the body hanging five in from there, so the `**-l**` definition of
§4 becomes

```
     -l   List in long format, giving mode, number of links, owner, size in
          bytes, and time of last modification for each file.
```

**A tag that leaves room keeps the body on its own line; a tag that does not gets the next
line.** "Leaves room" means the tag ends *before* the body column — at most four displayed
characters for the five-column step. This is the shape roff's `.TP` has always had.

### 10.3 The banner

The level-1 title is not rendered as text. It becomes a page banner: the page's own name at
both margins with the manual's name centred between them.

```
LS(1)                      UNIX Programmer's Manual                       LS(1)
```

The centred form is used only when the three fields and their gaps fit the measure; a name too
wide gets the left field alone. The `[extra]` qualifier goes on a **centred second line**, in
roman. There need be no footer: a pager supplies the paging and there is no page number.

### 10.4 A literal run is quoted

`` `x` `` renders as `` `x' `` — a backquote and an apostrophe. **The quote characters are the
renderer's, not the source's**, and are set in roman: punctuation the renderer added, not part
of what is quoted. An HTML back end emits `<code>` and no quotes at all.

### 10.5 A cross-reference

**The name carries the font and the parentheses stay roman** — the traditional italic name with
roman parentheses, plus a link in HTML and a colour of its own where colour is available.

### 10.6 Tabs in a fenced block

A tab advances to the next eight-column stop **counted from the block's own column 0, not from
the page's**: the indent step is 5 and a stop is 8, so measuring from the page shifts every
column of a hand-aligned table by a different amount, while measuring from the block shifts the
whole of it by five and the alignment survives. An **empty line inside a fence carries no
indent** — an indent plus a newline is trailing whitespace.

### 10.7 A column is a character

Bytes `0x80`–`0xBF` are UTF-8 continuations and take no column of their own; without that a
page with non-ASCII text folds at half the width. Emitting still walks bytes, which is correct:
it emits, it does not measure.

### 10.8 Fonts, and the plain form

**A back end must render the four spans of §2 and the cross-reference of §3 distinguishably**,
by whatever means it has — SGR attributes and colour on a terminal, `<b>`/`<i>`/`<code>`/`<a>`
in HTML, backspace overstrike on a printing terminal. Bold and italic carry different meanings
(§2) and must not be collapsed into one another. Two obligations beyond that:

- **A plain form must exist**: no escape sequence of any kind, legible in a file, the fallback
  wherever attributes are unwanted. Quotation marks and a cross-reference's parentheses are
  punctuation and stay in it.
- **Attributes are opened and closed around every run** rather than tracked across the output.
  It costs a few bytes on a line that alternates fonts and buys what matters when a pager or a
  filter is downstream: no escape sequence outlives its run, so nothing can leave a terminal in
  an attribute, and output cut at any line boundary leaves both halves well formed.

The output width is an input to the renderer, since not every system can ask the terminal.

### 10.9 Degrading

A reader with fixed buffers should **degrade rather than truncate**: split an over-long group
or line and format the rest, and emit a block nested deeper than it can handle verbatim — which
is what the dialect already promises for anything it cannot parse. A single line must always
fit an empty buffer, or the reader can spin.

---

## 11. The roff correspondence

The disposition of every `-man` request, for anyone reading a page's history or converting one
by hand.

| request | becomes | | request | becomes |
|---|---|---|---|---|
| `.TH` | the level-1 title (§5) | | `.br` | the paragraph becomes a line block |
| `.SH` | `##` | | `.nf` `.fi` | a fenced block, or a line block where the region has fonts |
| `.SS` | `###` | | `.RS` `.RE` | a quote |
| `.PP` `.LP` | a paragraph break | | `.\"` | `<!-- … -->` |
| `.B` | `**…**` | | `` `x' `` | a literal run, `` `x` `` (§2) |
| `.I` | `*…*` | | `.de` `..` `.ds` | expanded at conversion time; **no macros** |
| `.BR` `.RB` `.IR` `.RI` `.BI` `.IB` | alternating runs, **joined with no space** — roff's own rule | | `.so` | the named header inlined as a fenced `c` block |
| `.TP`, `.IP tag` | a definition list | | `.ns` `.PD` | dropped — there is one list form (§4) |
| `.IP 1.` | an ordered list | | `.SM` | dropped — `nroff` rendered it roman anyway |
| `.IP \(bu` | a bullet list | | `.ta` | dropped — a fenced block preserves columns |
| `.IP` `.IP ""` | a quote — an indented display, not a definition | | `.if` | the `n` branch is taken, the `t` branch discarded |
| `.HP` | rewritten by hand as a definition list | | `.UC` | dropped — provenance survives as a comment |
| `.DT` `.ne` `.dt` `.tr` `.nh` `.hy` `.ft` `.ce` `.ad` `.pg` `.ti` `.in` | dropped | | | |

| escape | becomes | | escape | becomes |
|---|---|---|---|---|
| `\-` | `-` | | `\ ` | one space |
| `\fB` `\fI` `\fR` `\fP` | a run boundary; `\fR` and `\fP` both close | | `\w'…'u` `\h` | dropped — width padding a filled renderer does not need |
| `\"` | the rest of the line is a comment | | `\'` `` \` `` | `'` and `` ` `` — and where the pair quotes a word, a literal run |
| `\|` `\^` | nothing — the neighbours join | | `\s±N` | dropped — a terminal has one type size |
| `\*x` `\*(xx` | the page's own `.ds` text, substituted | | `\&` | kept: it is this dialect's joiner too |
| `\e` | `\\`, or a bare `\` inside a fenced block | | `\u` `\d` | `^` (§8) |
| | | | `\z` `\v` `\c` | rewritten by hand |

**The dialect has no macro facility and must not grow one**; a page wanting a repeated phrase
types it. Conversion from roff is mechanical only to a point: expect to fix a hand-built table
that became a filled paragraph, an `.HP` that wanted to be a definition list, and a dropped
`.ta`.
