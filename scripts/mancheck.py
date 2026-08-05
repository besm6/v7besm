#!/usr/bin/env python3
"""mancheck.py ROFF UMM [B6MAN2UMM] -- does a converted page still say what the roff said?

Compare a roff -man page against its Unix Manual Markdown conversion, using the
host's groff as the oracle.  Exit 0 if they agree.

THIS IS HOW A CONVERSION IS TRUSTED.  b6man2umm is mechanical and its own unit
tests say only that it does what it was told; this says that a particular page
came through.  cmd/man2umm/README.md is the procedure it belongs to -- every v7
program still to be ported arrives with a roff page, and this is the step between
converting it and deleting the roff.

Three axes, because a word diff alone is not enough: it strips markup, so a
converter that dropped every ** would pass it clean, and the font is half of what
a -man page encodes.

    W   every word, in order, an exact diff
    S   the ordered list of section and subsection headings, which W cannot see
    F   the font of every rendered character

ONE SIDE ONLY.  The dialect's own three streams come from `b6man2umm -t/-s/-f':
this file never parses Markdown.  It used to, in awk, and then the quote rule
existed twice and the two copies disagreed -- a greedy regex ran from the opening
backquote past the real close to the apostrophe in a possessive, where the
converter stopped.  Everything here is about groff's output.

THREE GROFF SETTINGS ARE NOT OPTIONAL, and each was measured rather than guessed:

    GROFF_NO_SGR=1  groff emits ANSI SGR by default, and then there is no
                    overstrike to read the fonts out of at all.
    -rHY=0          hyphenation splits words across lines, which destroys W.
    -rLL=1000n      a line nothing wraps on.  -rHY=0 stops hyphenation but NOT
                    the break after an existing hyphen, and `word-/aligned'
                    arriving as two words is a difference that is not real.

col(1) is deliberately not used: it drops every byte above 0x7f in the C locale,
which silently deletes an em dash, a multiplication sign and all the Cyrillic.
Reading the overstrike here gives the plain text and the fonts in one pass anyway.
"""

import os
import re
import subprocess
import sys

# groff at -Tutf8 renders punctuation the source wrote in ASCII.  Fold it back, so
# that a page's own UTF-8 is the only UTF-8 being compared -- and so that a quote
# is one byte on both sides rather than three on one of them.
FOLD = {
    "‘": "`", "’": "'", "“": '"', "”": '"',
    "‐": "-", "−": "-", "∣": "|", " ": " ",
    "ˆ": "^", "˜": "~", "´": "'", "∗": "*",
}

BULLET = "•"

# A cross-reference, as doc/Manual_Page_Format.md section 3 defines it.
XREF = re.compile(r"[A-Za-z_][A-Za-z0-9_.+-]*\([1-8][a-z]?\)")

GROFF_ARGS = ["-k", "-Tutf8", "-man", "-rHY=0", "-rLL=1000n", "-rIN=0", "-rcR=1"]


def run_groff(path):
    """Render the page.  Returns (text, stderr)."""
    env = dict(os.environ, GROFF_NO_SGR="1")
    p = subprocess.run(["groff", *GROFF_ARGS, path], capture_output=True, env=env)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def decode_overstrike(raw):
    """Split groff's output into lines of (character, font) pairs.

    Bold is a character struck over itself and italic is an underscore struck over
    the character -- so a BOLD UNDERSCORE and an ITALIC UNDERSCORE are the same
    three characters.  Mark those 'U' and settle them from their neighbours below:
    SYS_close is bold, proc_user_time is italic, and nothing local says which.
    """
    lines = []
    for line in raw.split("\n"):
        out, i, n = [], 0, len(line)
        while i < n:
            c = line[i]
            if i + 2 < n and line[i + 1] == "\b":
                over = line[i + 2]
                if c == "_" and over == "_":
                    font = "U"
                elif c == over:
                    font = "B"
                elif c == "_":
                    font = "I"
                    c = over
                else:
                    font = "R"
                    c = over
                out.append((FOLD.get(c, c), font))
                i += 3
                continue
            out.append((FOLD.get(c, c), "R"))
            i += 1
        lines.append(collapse_em(resolve_ambiguous(out)))
    return lines


def collapse_em(cells):
    r"""groff draws \(em as TWO em dashes on a terminal.  The source wrote one."""
    out = []
    for cell in cells:
        if out and cell[0] == "\u2014" and out[-1][0] == "\u2014":
            continue
        out.append(cell)
    return out


def resolve_ambiguous(cells):
    """Give each 'U' the font of its neighbours, preferring the marked one.

    An underscore that opens a run has roman before it and italic after; one that
    closes a run has it the other way round.
    """
    if not any(f == "U" for _, f in cells):
        return cells
    out = list(cells)
    for i, (c, f) in enumerate(out):
        if f != "U":
            continue
        nxt = next((g for _, g in out[i + 1:] if g != "U"), "R")
        prv = next((g for _, g in reversed(out[:i]) if g != "U"), "R")
        out[i] = (c, prv if nxt == "R" and prv != "R" else nxt)
    return out


def plain(cells):
    return "".join(c for c, _ in cells)


def v7_quotes(text):
    """Spans of v7 quoting in `text', as (start, end, width) over its characters.

    v7 writes a backquote, the text and an apostrophe to mean "quoted as itself",
    and doubles both for an outer level.  A literal run in the dialect has neither
    quote character, so F must drop them and W must not see them as part of a word.

    THIS MIRRORS mark_quotes() IN cmd/man2umm/escape.cpp AND MUST: an apostrophe is
    also an apostrophe, so the close cannot be found with a regex -- a greedy one
    runs past the real close to the possessive in "bar's", where the converter
    stops.  The rules are: open at a word boundary with no space after, close at a
    word boundary with no space before, no backquote or newline between, matching
    quote widths, and a length cap so an unbalanced quote costs a phrase.
    """
    CAP = 60
    spans, i, n = [], 0, len(text)
    while i < n:
        if text[i] != "`":
            i += 1
            continue
        if i and text[i - 1].isalnum():
            i += 1
            continue
        width = 2 if text[i + 1:i + 2] == "`" else 1
        after = text[i + width:i + width + 1]
        if not after or after.isspace():
            i += 1
            continue
        end = -1
        j = i + width
        while j < n and j - i <= CAP:
            c = text[j]
            if c in "`\n":
                break
            if c != "'":
                j += 1
                continue
            run = 0
            while text[j + run:j + run + 1] == "'":
                run += 1
            if run != width:
                j += 1
                continue
            if text[j - 1].isspace():
                break
            nxt = text[j + run:j + run + 1]
            if nxt.isalnum():
                j += 1
                continue
            end = j
            break
        if end < 0:
            i += 1
            continue
        spans.append((i, end, width))
        i = end + width
    return spans


def unquote(text):
    """Take out the characters a v7 quotation spends on its own delimiters.

    Not every apostrophe: signal(2) titles a subsection "The return path is the
    kernel's", and blanking every quote character would take that one too.
    """
    drop = set()
    for start, end, width in v7_quotes(text):
        drop.update(range(start, start + width))
        drop.update(range(end, end + width))
    return "".join(c for i, c in enumerate(text) if i not in drop)


def words_of(lines):
    """Every word groff rendered, with the quotation delimiters taken out.

    unquote() rather than a per-word rule, because a quotation can end in the
    middle of one: exp(3m) writes an l-suffixed name as a quoted l followed by
    the suffix, and a regex anchored at the end of the token cannot see it.
    """
    out = []
    for cells in lines:
        for w in unquote(plain(cells)).split():
            if w == BULLET:  # a list marker, not a word
                continue
            w = w.replace("^", "")
            if w:
                out.append(w)
    return out


def fonts_of(lines, synopsis_fenced):
    """One letter per rendered BYTE, matching what b6man2umm -f emits.

    Three foldings, and each is a decision doc/Manual_Page_Format.md records:

      * a cross-reference is written plain here and italic by roff, so both sides
        are forced to R;
      * a v7 quotation is a literal run, so its two quote characters are dropped
        and its content forced to R -- unless it sits in a heading, which is bold
        on both sides;
      * a C SYNOPSIS is a fenced block and a fence is roman (section 7), so where
        the dialect chose a fence, groff's bold in that section folds away.
    """
    out = []
    in_synopsis = False
    for cells in lines:
        text = plain(cells)
        heading = bool(text) and (text[0].isupper() or re.match(r"^ {3}\S", text))
        if text and text[0].isupper() and not text.startswith(" "):
            in_synopsis = text.startswith("SYNOPSIS")
        fold_all = in_synopsis and not heading and synopsis_fenced

        force = set()
        drop = set()
        for m in XREF.finditer(text):
            if m.start() and (text[m.start() - 1].isalnum() or text[m.start() - 1] == "_"):
                continue
            force.update(range(m.start(), m.end()))
        for start, end, width in v7_quotes(text):
            drop.update(range(start, start + width))
            drop.update(range(end, end + width))
            force.update(range(start + width, end))

        for i, (c, f) in enumerate(cells):
            # A bullet is a list marker on this side and a `- ' the dialect side
            # never emits; a caret is this format's superscript and roff has no
            # glyph for one.  Neither is a character either stream counts.
            if c.isspace() or c in (BULLET, "^") or i in drop:
                continue
            if fold_all or (i in force and not heading):
                f = "R"
            out.append(f * len(c.encode("utf-8")))
    return "".join(out)


def roff_headings(path):
    """The .SH and .SS arguments, as text rather than as roff source."""
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^\.S[HS] *(.*)$", line.rstrip("\n"))
        if not m:
            continue
        t = m.group(1).strip()
        if t.startswith('"'):
            t = t[1:]
        if t.endswith('"'):
            t = t[:-1]
        t = re.sub(r"\\f.", "", t)
        t = t.replace("\\(em", "—").replace("\\(mi", "-").replace("\\-", "-")
        t = t.replace("\\'", "'").replace("\\`", "`")
        t = re.sub(r"\\[|^&]", "", t)
        out.append(" ".join(unquote(t).split()))
    return out


def tool(binary, flag, path):
    p = subprocess.run([binary, flag, path], capture_output=True)
    return p.stdout.decode("utf-8", "replace").split("\n")


def report(name, axis, want, got, limit=40):
    import difflib
    sys.stderr.write(f"mancheck: {name}: {axis}\n")
    diff = list(difflib.unified_diff(want, got, "roff", "umm", lineterm=""))
    sys.stderr.write("\n".join(diff[:limit]) + "\n")


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__.split("\n")[0] + "\n")
        return 2
    roff, ummpath = argv[1], argv[2]
    binary = argv[3] if len(argv) > 3 else "b6man2umm"
    name = os.path.basename(roff)

    raw, err = run_groff(roff)

    # A .so the host cannot resolve: the five section 5 pages source a header out of
    # /usr/include, which is this tree's include/ and not the build machine's.  groff
    # renders nothing there while b6man2umm inlines the real declarations, so W
    # relaxes to "lost no word" and F is skipped.  Those pages are hand-checked.
    partial = "cannot open" in err
    if partial:
        sys.stderr.write(f"mancheck: {name}: groff cannot resolve a .so on this host -- "
                         "W relaxed to additions-only, F skipped\n")
    elif err.strip():
        sys.stderr.write(f"mancheck: {name}: groff complained:\n{err}")

    # Drop the running header and the page footer -- the first and last lines that
    # have anything on them.  -rcR=1 makes the page continuous, so there is exactly
    # one of each; groff may put a blank line either side of them.
    lines = decode_overstrike(raw)
    used = [i for i, cells in enumerate(lines) if plain(cells).strip()]
    if len(used) > 2:
        lines = lines[used[0] + 1:used[-1]]

    status = 0

    want = words_of(lines)
    got = [w for w in (x.replace("^", "") for x in tool(binary, "-t", ummpath)) if w]
    if want != got:
        lost = [w for w in want if w not in got]
        if not (partial and not lost):
            report(name, "W -- the word stream differs", want, got)
            status = 1

    want = roff_headings(roff)
    got = [h for h in tool(binary, "-s", ummpath) if h.strip()]
    if want != got:
        report(name, "S -- the section structure differs", want, got)
        status = 1

    if not partial:
        umm = open(ummpath, encoding="utf-8", errors="replace").read()
        m = re.search(r"^## SYNOPSIS\s*\n\s*\n(.*)$", umm, re.M)
        fenced = bool(m) and m.group(1).startswith("```")
        want = fonts_of(lines, fenced)
        got = "".join(tool(binary, "-f", ummpath)).replace("X", "R").replace("L", "R")
        if want != got:
            i = next((i for i, (a, b) in enumerate(zip(want, got)) if a != b),
                     min(len(want), len(got)))
            sys.stderr.write(f"mancheck: {name}: F -- the font of some character differs\n")
            sys.stderr.write(f"  at {i} of {len(want)}/{len(got)}\n")
            sys.stderr.write(f"  roff {want[max(0, i - 30):i + 30]}\n")
            sys.stderr.write(f"  umm  {got[max(0, i - 30):i + 30]}\n")
            text = "".join(plain(c) for c in lines).replace(" ", "")
            sys.stderr.write(f"  text {text[max(0, i - 30):i + 30]!r}\n")
            status = 1

    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
