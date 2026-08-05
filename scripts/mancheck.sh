#!/bin/sh
#
# mancheck.sh ROFF UMM [B6MAN2UMM]
#
# Compare a roff -man page against its Unix Manual Markdown conversion, using the
# host's groff as the oracle.  Exit 0 if they agree.
#
# THIS IS HOW A CONVERSION IS TRUSTED.  b6man2umm is mechanical and its own unit
# tests say only that it does what it was told; this says that a particular page
# came through.  cmd/man2umm/README.md is the procedure it belongs to -- every v7
# program still to be ported arrives with a roff page, and this is the step between
# converting it and deleting the roff.
#
# Three axes, because a word-stream diff alone is not enough: it strips markup, so a
# converter that dropped every ** would pass it clean, and the font is half of what
# a -man page encodes.
#
#   W  THE WORD STREAM.  An exact ordered diff of every word.  Not containment: a
#      gained or transposed word fails too, and a join/split error -- the failure
#      mode the \| and \& and the alternator rule make likely -- shows up as two
#      lines of diff.
#
#   S  THE STRUCTURE.  The ordered list of section and subsection headings.  W
#      cannot see this: it accepts a `## SEE ALSO' demoted to a paragraph.
#
#   F  THE FONT OF EVERY CHARACTER.  groff without col(1) emits v7's own overstrike
#      -- c\bc for bold, _\bc for italic -- so each rendered character can be
#      labelled B, I or R and diffed against `b6man2umm -f'.  This is the axis that
#      closes W's gap, and the reason ummread.cpp is built now rather than with the
#      renderer.
#
# THREE GROFF FLAGS ARE NOT OPTIONAL, and each was measured rather than guessed:
#
#   GROFF_NO_SGR=1   groff emits ANSI SGR by default, so col -b never sees an
#                    overstrike and F would compare nothing at all.
#   -rHY=0           hyphenation splits words across lines ("stan-dard"), which
#                    destroys a word stream outright.
#   -rcR=1           one continuous page, so there is one running header and one
#                    footer to strip rather than one of each per 66 lines.
#   -rLL=1000n       a line nothing wraps on.  -rHY=0 stops hyphenation but NOT the
#                    break after an existing hyphen, and `word-/aligned' arriving as
#                    two words is a word-stream difference that is not real.
#
# -Tutf8 -k because ten pages carry Cyrillic; the normalizer folds the typographic
# punctuation that -Tutf8 then produces back to ASCII.
#
set -u

ROFF=$1
UMM=$2
B6MAN2UMM=${3:-b6man2umm}
TMP=${TMPDIR:-/tmp}/mancheck.$$
mkdir -p "$TMP" || exit 2
trap 'rm -rf "$TMP"' 0 1 2 3 15

status=0
name=$(basename "$ROFF")

# ---------------------------------------------------------------- the rendering ----
# Once, with overstrike intact.  W and S read it through col -b; F reads it raw.
#
# THE PUNCTUATION FOLD, and it is applied to groff's output BEFORE anything reads
# it, so that all three axes see one normalized stream.  -Tutf8 renders an ASCII
# backquote as U+2018 and a \(mi as U+2212 -- three bytes where the source had one.
# W would then see a different word, and F, which counts bytes because the dialect
# side does, would see two extra characters per quote.
#
fold_punct='s/\xe2\x80\x98/`/g; s/\xe2\x80\x99/'"'"'/g; s/\xe2\x80\x90/-/g; s/\xe2\x88\x92/-/g; s/\xe2\x80\x9c/"/g; s/\xe2\x80\x9d/"/g; s/\xe2\x88\xa3/|/g; s/\xc2\xa0/ /g; s/\xcb\x86/^/g; s/\xcb\x9c/~/g; s/\xc2\xb4/'"'"'/g'

GROFF_NO_SGR=1 groff -k -Tutf8 -man -rHY=0 -rLL=1000n -rIN=0 -rcR=1 "$ROFF" \
    2>"$TMP/grofferr" | sed "$fold_punct" > "$TMP/raw"
#
# A .so THE HOST CANNOT RESOLVE.  The five section 5 pages source a header out of
# /usr/include, which is this tree's include/ and not the build machine's; groff
# renders nothing there while b6man2umm inlines the real declarations.  The oracle
# then has nothing to say about that content, so W relaxes to "lost no word" (the
# dialect may have gained some) and F is skipped.  Those pages are hand-checked.
#
partial=0
if [ -s "$TMP/grofferr" ]; then
    if grep -q "cannot open" "$TMP/grofferr"; then
        partial=1
        echo "mancheck: $name: groff cannot resolve a .so on this host --" \
             "W relaxed to additions-only, F skipped" >&2
    else
        echo "mancheck: $name: groff complained:" >&2
        cat "$TMP/grofferr" >&2
    fi
fi

# The running header and footer groff puts on the one page: the title line at the
# top and the page number at the bottom, neither of which is content.
col -b < "$TMP/raw" | sed -e '1d' -e '$d' > "$TMP/plain"

# ---------------------------------------------------------------- W, the words ----
tr -s ' \t' '\n\n' < "$TMP/plain" \
    | sed -e '/^$/d' -e '/^\xe2\x80\xa2$/d' \
          -e 's/^`//' -e "s/'\\([^A-Za-z0-9]*\\)\$/\\1/" > "$TMP/w.roff"

# The Markdown side.  awk rather than sed because A FENCED BLOCK IS VERBATIM: its
# asterisks and backticks are content, and stripping them there is how `char *buffer'
# loses its star.  The escapes are parked on control characters first, so that the
# delimiter pass cannot eat a `\`' that stands for a backtick.
awk '
# Take the inline markup off.  Two things it must NOT do: eat the stars of 2**40,
# whose run has a digit on either side and so is text by rule 2; and eat the star of
# a literal run, `*\x27 being a quoted asterisk and its content verbatim.
function strip_inline(str,   out, i, n, run, c, before, after, endq) {
    out = ""; n = length(str); i = 1
    while (i <= n) {
        c = substr(str, i, 1)
        if (c == "`") {              # a literal run: its content is verbatim
            endq = index(substr(str, i + 1), "`")
            if (endq == 0) { i++; continue }
            out = out substr(str, i + 1, endq - 1)
            i += endq + 1
            continue
        }
        if (c != "*") { out = out c; i++; continue }
        run = 0
        while (i + run <= n && substr(str, i + run, 1) == "*") run++
        before = (i > 1) ? substr(str, i - 1, 1) : ""
        after  = (i + run <= n) ? substr(str, i + run, 1) : ""
        if (before ~ /[A-Za-z0-9]/ && after ~ /[A-Za-z0-9]/) out = out substr(str, i, run)
        i += run
    }
    return out
}
/^[ >]*```/ { fence = 1 - fence; next }
fence       { line = $0; sub(/^ *> ?/, "", line); print line; next }
/^<!--/     { next }
/^# /       { next }
{
    line = $0
    gsub(/\\\*/, "\001", line)
    gsub(/\\`/,  "\002", line)
    gsub(/\\\\/, "\003", line)
    sub(/^###? /, "", line)
    # A container prefix can nest: a definition whose body is a line block is
    # written as a colon, a space, a bar and a space.  So peel until none is left.
    # An ESCAPED block character never matches, the backslashes surviving to below.
    do {
        peeled = 0
        if (sub(/^ *: /, "", line)) peeled = 1
        if (sub(/^ *> /, "", line)) peeled = 1
        if (sub(/^ *- /, "", line)) peeled = 1
        if (sub(/^ *[0-9]+\. /, "", line)) peeled = 1
        if (sub(/^ *\| /, "", line)) peeled = 1
    } while (peeled)
    gsub(/\\&/, "", line)
    line = strip_inline(line)
    gsub(/\\/, "", line)     # what is left is an escaped block-start character
    gsub(/\001/, "*", line)
    gsub(/\002/, "`", line)
    gsub(/\003/, "\\", line)
    print line
}
' "$UMM" | tr -s ' \t' '\n\n' \
    | sed -e '/^$/d' -e 's/^`//' -e "s/'\\([^A-Za-z0-9]*\\)\$/\\1/" > "$TMP/w.umm"

if ! diff -u "$TMP/w.roff" "$TMP/w.umm" > "$TMP/w.diff"; then
    lost=$(grep -c '^-[^-]' "$TMP/w.diff")
    if [ "$partial" = 1 ] && [ "$lost" = 0 ]; then
        : # only additions, which is what an unresolved .so looks like
    else
        echo "mancheck: $name: W -- the word stream differs" >&2
        sed -n '1,40p' "$TMP/w.diff" >&2
        status=1
    fi
fi

# ------------------------------------------------------------ S, the structure ----
# From the roff, the .SH and .SS arguments, dequoted.  From the dialect, the ## and
# ### lines with their inline markup taken off.
sed -n -e 's/^\.S[HS] *//p' "$ROFF" \
    | sed -e 's/^"//' -e 's/"$//' \
          -e 's/\\f.//g' -e 's/\\(em/\xe2\x80\x94/g' -e 's/\\(mi/-/g' \
          -e 's/\\-/-/g' -e "s/\\\\'/'/g" -e 's/\\`/`/g' -e 's/\\[|^&]//g' \
          -e 's/  */ /g' -e 's/^ //' -e 's/ $//' > "$TMP/s.roff"
sed -n -e 's/^#\{2,3\} //p' "$UMM" \
    | sed -e 's/\\\*/\x01/g' -e 's/\\`/\x02/g' \
          -e 's/\*\*//g' -e 's/\*//g' -e 's/`//g' -e 's/\\//g' \
          -e 's/\x01/*/g' -e 's/\x02/`/g' > "$TMP/s.umm"

if ! diff -u "$TMP/s.roff" "$TMP/s.umm" > "$TMP/s.diff"; then
    echo "mancheck: $name: S -- the section structure differs" >&2
    sed -n '1,40p' "$TMP/s.diff" >&2
    status=1
fi

# ---------------------------------------------------------------- F, the fonts ----
# groff's overstrike, one letter per rendered non-space character.  Four folds, and
# every one of them is a decision doc/Manual_Page_Format.md records:
#
#   a cross-reference is X on the dialect side and italic-plus-roman on groff's, so
#   both are forced to R; a heading is bold to groff and a ## here, so the dialect's
#   headings are already emitted as B; a literal run was roman under roff, so L
#   folds to R.
# ONE OUTPUT LINE PER INPUT LINE, so that the page header and footer can be dropped
# by line number exactly as W drops them.  (groff sets the running header in italic,
# so it cannot be told from content by its font.)  That makes F an exact diff rather
# than a search, which is the whole point of having it.
awk '
{
    out = ""
    n = length($0)
    for (i = 1; i <= n; ) {
        c = substr($0, i, 1)
        if (i + 2 <= n && substr($0, i + 1, 1) == "\b") {
            over = substr($0, i + 2, 1)
            if (c == "_")       out = out "I"
            else if (c == over) out = out "B"
            else                out = out "R"
            i += 3
            continue
        }
        if (c != " " && c != "\t" && c != "\b") out = out "R"
        i++
    }
    print out
}
' "$TMP/raw" | sed -e '1d' -e '$d' > "$TMP/flines"

#
# A C SYNOPSIS IS A FENCE, AND A FENCE IS ROMAN -- doc/Manual_Page_Format.md section
# 7 drops the font there on purpose, a declaration having no font distinctions to
# keep.  So where the dialect chose a fence, fold groff's bold in that section too;
# where it chose a line block, the fonts are compared as everywhere else.
#
syn_fenced=$(awk '/^## SYNOPSIS/ { want = 1; next }
                  want && /^[ ]*$/ { next }
                  want { print (/^```/) ? 1 : 0; exit }' "$UMM")
[ -n "${syn_fenced:-}" ] || syn_fenced=0

awk -v fence="$syn_fenced" -v q="'" '
NR == FNR { text[FNR] = $0; next }
{
    t = text[FNR]
    f = $0

    #
    # A CROSS-REFERENCE IS ROMAN ON BOTH SIDES.  groff sets the name of
    # open(2) in italic because the page wrote .IR open (2); the dialect
    # writes it plain and lets the renderer choose.  So find every
    # reference in the rendered text and force its characters to R here,
    # exactly as the dialect side folds its X to R.
    #
    mark = ""
    while (length(mark) < length(t)) mark = mark "0"
    off = 0
    s2 = t
    while (match(s2, /[A-Za-z_][A-Za-z0-9_.+-]*\([1-8][a-z]?\)/)) {
        st = off + RSTART
        en = st + RLENGTH - 1
        off = st
        s2 = substr(t, off + 1)
        if (st > 1 && substr(t, st - 1, 1) ~ /[A-Za-z0-9_]/) continue
        for (i = st; i <= en; i++)
            mark = substr(mark, 1, i - 1) "1" substr(mark, i + 1)
        off = en
        s2 = substr(t, off + 1)
    }

    #
    # v7 QUOTING.  A backquote, a word and an apostrophe is a literal run here, so
    # the dialect has neither quote character and the content is roman.  Mark the
    # quotes to be dropped from this stream (2) and their content forced to R (1).
    #
    # THE SCAN MIRRORS mark_quotes() IN cmd/man2umm/escape.cpp AND MUST.  A regex
    # cannot: it is greedy, so in a line holding a quotation and a possessive it
    # runs from the opening backquote past the closing apostrophe to the one in
    # "bar\47s", where the converter stops at the first apostrophe that is not
    # inside a word.  The two have to agree character for character or this axis
    # reports differences that are only the two rules disagreeing.
    #
    qi = 1
    tn = length(t)
    while (qi <= tn) {
        if (substr(t, qi, 1) != "`") { qi++; continue }
        if (qi > 1 && substr(t, qi - 1, 1) ~ /[A-Za-z0-9]/) { qi++; continue }
        qc = substr(t, qi + 1, 1)
        if (qc == "" || qc == " " || qc == "\t") { qi++; continue }
        qe = 0
        for (qj = qi + 1; qj <= tn && qj - qi <= 60; qj++) {
            qc = substr(t, qj, 1)
            if (qc == "`") break
            if (qc != q) continue
            if (substr(t, qj - 1, 1) == " ") break
            if (substr(t, qj + 1, 1) ~ /[A-Za-z0-9]/) continue
            qe = qj
            break
        }
        if (qe == 0 || qe == qi + 1) { qi++; continue }
        mark = substr(mark, 1, qi - 1) "2" substr(mark, qi + 1)
        mark = substr(mark, 1, qe - 1) "2" substr(mark, qe + 1)
        for (qk = qi + 1; qk < qe; qk++)
            mark = substr(mark, 1, qk - 1) "1" substr(mark, qk + 1)
        qi = qe + 1
    }

    head = (t ~ /^[A-Z]/)
    if (head) insyn = (t ~ /^SYNOPSIS/)
    fold_all = (insyn && !head && fence == 1)

    out = ""
    k = 0
    n = length(t)
    for (i = 1; i <= n; i++) {
        c = substr(t, i, 1)
        if (c == " " || c == "\t") continue
        k++
        ch = substr(f, k, 1)
        if (substr(mark, i, 1) == "2") continue
        if (fold_all || substr(mark, i, 1) == "1") ch = "R"
        out = out ch
    }
    printf "%s", out
}
' "$TMP/plain" "$TMP/flines" > "$TMP/f.roff"

"$B6MAN2UMM" -f "$UMM" 2>/dev/null | tr -d '\n' | sed -e 's/X/R/g' -e 's/L/R/g' > "$TMP/f.umm"

if [ "$partial" = 0 ] && ! diff -u "$TMP/f.roff" "$TMP/f.umm" > "$TMP/f.diff"; then
    echo "mancheck: $name: F -- the font of some character differs" >&2
    echo "  roff: $(head -c 400 "$TMP/f.roff")" >&2
    echo "  umm:  $(head -c 400 "$TMP/f.umm")" >&2
    status=1
fi

exit $status
