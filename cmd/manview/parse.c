//
// The dialect reader: Unix Manual Markdown -> the out_*() calls of render.c.
//
// ../man2umm/ummread.cpp IS THE SPECIFICATION and this is that file transliterated.  Every
// recognition rule here is its rule -- the flanking test of format rule 2, the padded
// backtick, enum_label(), starts_block(), take_indented()'s blank-line clause -- and where
// the two disagree ummread.cpp is right, since it is the half of the converter that closes
// the round trip.  It parses the dialect and only the dialect: there is no link syntax, no
// HTML, no setext heading, no thematic break, and an unrecognized construct is text.
//
// WHAT IS NOT TRANSLITERATED IS THE DOCUMENT MODEL, and that is the whole difference.
// ummread.cpp builds a Doc of Block and Span vectors and hands it to a back end; on this
// machine the largest page in the tree, ../sh/sh.1.umm, is 19,401 bytes and a span tree
// over it would be several times that against an address space of 28,672 words
// (../README.md §6).  So nothing is built: the reader renders as it recognizes.
//
// THE MEASUREMENT THAT MAKES THAT POSSIBLE is that a page does not have to be held at all,
// only a GROUP of it -- a run of lines delimited by a blank line that is followed by a
// line at column 0, fenced blocks excepted.  Over the 202 pages of this tree the largest
// group is 4,053 bytes and 71 lines (../../lib/libcurses/curses.3.umm), so the buffer
// below is twice the worst case that exists and a page of any length streams through it.
//
// AND WHAT THE MODEL WAS CARRYING IS CARRIED BY THE RECURSION INSTEAD.  parse_blocks()
// recurses over sliced vectors of lines; this recurses over (lo, hi, strip) index ranges
// into the one group, because a container's body is stripped by a UNIFORM width -- two
// columns, or the `> ', `- ', `: ', `N. ' prefix -- which is exactly what take_item() has.
// No per-level array, and a frame of a dozen words against a 4,096-word stack.
//
// <ctype.h> IS NOT INCLUDED AND NOT WANTED.  That table has 129 entries (../README.md §11)
// and this reader is handed UTF-8: seventeen pages here have Cyrillic in them, and
// isalnum() on the first byte of `привет' indexes past the end of it.  The four
// classifiers below answer for ASCII and answer `no' above it, which is what the C locale
// would have said and what the flanking rule means.
//
#include <stdio.h>
#include <string.h>

#include "manview.h"

#define GBUFSZ  8192 // one group of lines -- twice the corpus's worst case
#define GLINES  128  // ... and its line count, likewise
#define LINESZ  2048 // one source line; the corpus's longest is 945 bytes
#define TITLESZ 128  // the level-1 title and its bracketed qualifier

//
// A CEILING ON THE NESTING, which §6 of ../README.md asks of any recursion whose depth the
// input chooses.  blocks() carries a 163-word frame, so a page indented two columns at a
// time could ask for a thousand of them against a 4,096-word stack.  The deepest nesting in
// the 202 pages of this tree is a leading indent of THREE columns -- one container inside
// another -- so eight is four times what the corpus has ever wanted, and a page past it is
// shown as it stands rather than formatted, which is what the dialect promises anyway.
//
#define MAXDEPTH 8

static char gbuf[GBUFSZ];
static char *gline[GLINES];
static int gn;    // lines in the group
static int gused; // bytes of gbuf they occupy

static char hold[LINESZ]; // the one line of lookahead the group reader needs
static int holdful;
static int overflow;

// ---- the classifiers, for ASCII and deliberately not above it -------------------------

static int isdig(char c)
{
    return c >= '0' && c <= '9';
}

static int islow(char c)
{
    return c >= 'a' && c <= 'z';
}

static int isalpha_(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int isalnum_(char c)
{
    return isalpha_(c) || isdig(c);
}

static int isblnk(char c)
{
    return c == ' ' || c == '\t';
}

// True if `s' begins with the whole of `pre'.
static int has(const char *s, const char *pre)
{
    return strncmp(s, pre, strlen(pre)) == 0;
}

// ---- the group reader ------------------------------------------------------------------

// One source line into `hold', without its newline.  0 at end of file.
static int readline(FILE *fp)
{
    int n;

    if (fgets(hold, LINESZ, fp) == 0)
        return 0;
    n = (int)strlen(hold);
    if (n > 0 && hold[n - 1] == '\n')
        hold[--n] = '\0';
    if (n > 0 && hold[n - 1] == '\r')
        hold[n - 1] = '\0';
    return 1;
}

static int allblank(const char *s)
{
    while (isblnk(*s))
        s++;
    return *s == '\0';
}

// A fence opens or closes wherever it stands: at column 0 at the top level, indented
// under a definition.  Leading spaces are the container's, not the fence's.
static int isfence(const char *s)
{
    while (*s == ' ')
        s++;
    return has(s, "```");
}

static int push(const char *s)
{
    int n;

    n = (int)strlen(s);
    if (gn >= GLINES || gused + n + 1 > GBUFSZ)
        return 0;
    gline[gn++] = gbuf + gused;
    memcpy(gbuf + gused, s, n + 1);
    gused += n + 1;
    return 1;
}

//
// Collect one group.  A blank line ends it only when what follows is at column 0: the
// indented continuation of a definition, a list item or a quote belongs to the block above
// it, which is take_indented()'s rule and the reason a body may hold more than one
// paragraph.  Inside a fenced block nothing ends anything.
//
// A GROUP THAT WILL NOT FIT is rendered as far as it was read and the line that would not
// go in starts the next one, so a page too big for the buffer degrades rather than
// truncating and cannot spin: a single line always fits an empty group.
//
static int read_group(FILE *fp)
{
    int infence, blanks;

    gn      = 0;
    gused   = 0;
    infence = 0;
    for (;;) {
        if (!holdful) {
            if (!readline(fp))
                break;
            holdful = 1;
        }
        if (gn == 0) {
            if (allblank(hold)) { // the separator before the group
                holdful = 0;
                continue;
            }
        } else if (!infence && allblank(hold)) {
            blanks = 0;
            do {
                blanks++;
                holdful = 0;
                if (!readline(fp))
                    return gn; // end of file: the group ends with it
                holdful = 1;
            } while (allblank(hold));
            if (hold[0] != ' ')
                return gn; // a line at column 0: a new group
            while (blanks-- > 0)
                if (!push("")) {
                    overflow = 1;
                    return gn;
                }
        }
        if (isfence(hold))
            infence = !infence;
        if (!push(hold)) {
            overflow = 1;
            return gn; // `hold' is kept: it opens the next group
        }
        holdful = 0;
    }
    return gn;
}

// ---- the lines of one group, with a container's prefix taken off -----------------------

// Line `i' with `strip' columns removed.  A blank or short line answers empty, which is
// what take_indented() pushes for one.  Not const: the group buffer is this file's, and a
// paragraph is joined and a heading trimmed by writing into it.
static char *at(int i, int strip)
{
    char *s;
    int k;

    s = gline[i];
    for (k = 0; k < strip; k++) {
        if (s[k] == '\0')
            return s + k;
    }
    return s + strip;
}

static int blankat(int i, int strip)
{
    return allblank(at(i, strip));
}

static int indented(int i, int strip, int w)
{
    const char *s;
    int k;

    s = at(i, strip);
    for (k = 0; k < w; k++)
        if (s[k] != ' ')
            return 0;
    return 1;
}

//
// take_indented(): one past the last line of a body indented by `w'.  A blank line belongs
// to the body only when indented text follows it -- otherwise a definition would swallow
// the next section.
//
static int body_end(int i, int hi, int strip, int w)
{
    int j;

    while (i < hi) {
        if (blankat(i, strip)) {
            j = i;
            while (j < hi && blankat(j, strip))
                j++;
            if (j >= hi || !indented(j, strip, w))
                break;
            i = j;
            continue;
        }
        if (!indented(i, strip, w))
            break;
        i++;
    }
    return i;
}

// `N. ' at the head of a line: an ordered-list item.  *width is what to strip.
static int enum_label(const char *s, int *width)
{
    int i;

    for (i = 0; isdig(s[i]); i++)
        ;
    if (i == 0 || s[i] != '.' || s[i + 1] != ' ')
        return 0;
    *width = i + 2;
    return 1;
}

// Does this line begin a block other than a paragraph?
static int starts_block(const char *s)
{
    int w;

    return has(s, "```") || has(s, "## ") || has(s, "### ") || has(s, "<!-- ") ||
           has(s, "| ") || strcmp(s, "|") == 0 || has(s, "> ") || strcmp(s, ">") == 0 ||
           has(s, "- ") || has(s, ": ") || enum_label(s, &w);
}

// ---- inline markup ---------------------------------------------------------------------

//
// A cross-reference, section 3: a name, `(', a section digit, an optional lowercase letter,
// `)', and no space anywhere in it.  ../man2umm/escape.cpp's xref_at() is the grammar.
// *namelen is where the name ends and the roman parentheses begin.
//
static int xref_at(const char *s, int i, int *namelen, int *end)
{
    int j;

    if (!isalpha_(s[i]) && s[i] != '_')
        return 0;
    j = i;
    while (isalnum_(s[j]) || s[j] == '_' || s[j] == '.' || s[j] == '+' || s[j] == '-')
        j++;
    if (j == i || s[j] != '(')
        return 0;
    *namelen = j - i;
    j++;
    if (s[j] < '1' || s[j] > '8')
        return 0;
    j++;
    if (islow(s[j]))
        j++;
    if (s[j] != ')')
        return 0;
    *end = j + 1;
    return 1;
}

//
// THE BASE FONT is what unmarked text of the run is in, and it is Roman everywhere but a
// heading.  ummread.cpp parses a `## X' the same way it parses a paragraph -- the heading
// is spans, not a string -- so `### `end` is declared as an array' has a literal run in it
// and must render as one.  Setting the base rather than special-casing the heading is what
// keeps one scanner for both.
//
static int basefont = F_ROMAN;

static void emit(int font, const char *p, int n)
{
    if (n > 0)
        out_span(font == F_ROMAN ? basefont : font, p, n);
}

//
// One line of source into runs.  Rule 2's flanking test decides whether a `*' is a
// delimiter at all, which is what lets `2**41-1' through as text: its asterisks have a
// digit on both sides.  Rule 3's `\&' is dropped and joins its neighbours into one word,
// which is the whole of what it is for.
//
// A CROSS-REFERENCE IS LOOKED FOR IN ROMAN TEXT ONLY, where ummread.cpp runs mark_xrefs()
// over the finished spans with respect_markup set.  The two agree: that flag exists to
// leave `**open(2)**' alone, canonical-shape rule 9 forbids writing one, and a match inside
// a literal run is not a reference either.  Neither can be reached from here.
//
// IT IS HANDED A WHOLE PARAGRAPH AND NOT A LINE, because a marked run may cross a source
// line: ../ls/ls.1.umm opens a bold run on one line and closes it on the next, and
// ../../lib/libc/man/getc.3s.umm does it with a literal run.  ummread.cpp meets that by
// joining a paragraph's lines before it looks at any of them and blocks() does the same,
// in place.  A line block and a heading are one line each there and here.
//
static void spans(const char *s)
{
    int i, n, start, font, run, want, lok, rok, wide, clen, cs, j, namelen, end;

    n     = (int)strlen(s);
    start = 0;
    font  = F_ROMAN;
    i     = 0;
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {
            emit(font, s + start, i - start);
            if (s[i + 1] != '&') // the joiner is not a character
                emit(font, s + i + 1, 1);
            i += 2;
            start = i;
            continue;
        }

        if (s[i] == '`' && font == F_ROMAN) {
            // `` x `` is the padded form, for a literal run holding a backtick.
            wide = (i + 1 < n && s[i + 1] == '`');
            clen = wide ? 2 : 1;
            cs   = i + clen;
            for (j = cs; j + clen <= n; j++)
                if (strncmp(s + j, wide ? "``" : "`", clen) == 0)
                    break;
            if (j + clen > n) { // no close: the backtick is text
                i++;
                continue;
            }
            end = j;
            if (wide) {
                if (cs < end && s[cs] == ' ')
                    cs++;
                if (cs < end && s[end - 1] == ' ')
                    end--;
            }
            emit(font, s + start, i - start);
            out_span(F_LITERAL, s + cs, end - cs);
            i     = j + clen;
            start = i;
            continue;
        }

        if (s[i] == '*') {
            run  = (i + 1 < n && s[i + 1] == '*') ? 2 : 1;
            want = run == 2 ? F_BOLD : F_ITALIC;
            if (font == want) { // a closing delimiter?
                lok = i > 0 && !isblnk(s[i - 1]);
                rok = i + run >= n || !isalnum_(s[i + run]);
                if (lok && rok) {
                    emit(font, s + start, i - start);
                    font  = F_ROMAN;
                    i += run;
                    start = i;
                    continue;
                }
            } else if (font == F_ROMAN) { // an opening one?
                lok = i == 0 || !isalnum_(s[i - 1]);
                rok = i + run < n && !isblnk(s[i + run]);
                if (lok && rok) {
                    emit(font, s + start, i - start);
                    font  = want;
                    i += run;
                    start = i;
                    continue;
                }
            }
            i += run;
            continue;
        }

        // A name must start at a word boundary, or `printf' inside `sprintf(3)' matches.
        if (font == F_ROMAN && !(i > 0 && (isalnum_(s[i - 1]) || s[i - 1] == '_')) &&
            xref_at(s, i, &namelen, &end)) {
            emit(font, s + start, i - start);
            emit(F_XREF, s + i, namelen);
            emit(F_ROMAN, s + i + namelen, end - i - namelen); // the parentheses stay roman
            i     = end;
            start = i;
            continue;
        }

        i++;
    }
    emit(font, s + start, n - start);
}

// ---- the blocks --------------------------------------------------------------------------

static void blocks(int lo, int hi, int strip, int col, int depth);

//
// A container: its marker rendered as a hanging tag, its body one step further in.  The
// body is [lo, end) of the same group with `w' more columns taken off, exactly as
// take_item() slices it -- the first line loses the marker and the rest lose the indent,
// and both are `w' wide, which is why one number does for the whole range.
//
static void container(int lo, int hi, int strip, int col, int w, const char *tag, int depth)
{
    int end;

    end = body_end(lo + 1, hi, strip, w);
    out_blank();
    out_fill(1);
    out_indent(col);
    if (tag != 0)
        spans(tag);
    out_hang(col + STEP);
    out_nogap(); // the body begins on the tag's line if the tag left room
    blocks(lo, end, strip + w, col + STEP, depth);
}

//
// The level-1 title of section 5: `# LS(1)', `# GETPW(3) [deprecated]'.  It is the one
// block whose rendering is not a rendering of its text -- render.c makes a banner of it.
//
static void title(const char *s)
{
    static char tbuf[TITLESZ];
    static char ebuf[TITLESZ];
    int n, lb;

    while (*s == ' ')
        s++;
    n = (int)strlen(s);
    while (n > 0 && isblnk(s[n - 1]))
        n--;
    if (n >= TITLESZ)
        n = TITLESZ - 1;
    memcpy(tbuf, s, n);
    tbuf[n]  = '\0';
    ebuf[0]  = '\0';

    for (lb = 0; lb + 1 < n; lb++)
        if (tbuf[lb] == ' ' && tbuf[lb + 1] == '[')
            break;
    if (lb + 1 < n && tbuf[n - 1] == ']') {
        memcpy(ebuf, tbuf + lb + 2, n - lb - 3);
        ebuf[n - lb - 3] = '\0';
        tbuf[lb]         = '\0';
    }
    out_banner(tbuf, ebuf);
}

static void blocks(int lo, int hi, int strip, int col, int depth)
{
    char *s;
    char label[8];
    int i, j, w, end;

    if (depth >= MAXDEPTH) {
        out_blank();
        out_fill(0);
        out_indent(col);
        for (i = lo; i < hi; i++)
            out_verbatim(at(i, strip));
        out_fill(1);
        return;
    }
    depth++;
    i = lo;
    while (i < hi) {
        if (blankat(i, strip)) {
            i++;
            continue;
        }
        s = at(i, strip);

        //
        // THE SOURCE'S OWN BLANK LINES DECIDE THE SPACING, which is the only rule that can
        // tell a compact list from a spaced one here: the format dropped roff's .PD and .ns
        // because it has one list form (section 11), so what is left to read is whether the
        // author put a blank line between two items.  A block that follows its predecessor
        // with nothing between them is set with nothing between them.
        //
        if (i > lo && !blankat(i - 1, strip))
            out_nogap();

        if (has(s, "```")) {
            out_blank();
            out_fill(0);
            out_indent(col);
            for (i++; i < hi && !has(at(i, strip), "```"); i++)
                out_verbatim(at(i, strip));
            if (i < hi)
                i++; // the closing fence
            out_fill(1);
            continue;
        }

        if (has(s, "<!-- ")) { // provenance, and not rendered
            i++;
            continue;
        }

        if (has(s, "### ") || has(s, "## ")) {
            w = has(s, "### ") ? 4 : 3;
            for (j = (int)strlen(s); j > w && isblnk(s[j - 1]); j--)
                ;
            s[j]     = '\0';
            basefont = w == 4 ? F_SUBHEAD : F_HEAD;
            out_blank();
            out_fill(0);
            out_indent(w == 4 ? SUBCOL : 0);
            spans(s + w); // a heading is spans and not a string: `### `end` is an array'
            basefont = F_ROMAN;
            out_break();
            out_fill(1);
            out_nogap(); // v7 puts no blank line between a heading and its body
            i++;
            continue;
        }

        if (has(s, "# ")) {
            out_blank();
            title(s + 2);
            i++;
            continue;
        }

        if (has(s, "| ") || strcmp(s, "|") == 0) {
            out_blank();
            out_fill(0);
            out_indent(col);
            while (i < hi) {
                s = at(i, strip);
                if (!has(s, "| ") && strcmp(s, "|") != 0)
                    break;
                s += s[1] == '\0' ? 1 : 2;
                // A bare `|' is an empty LINE and not an empty block: out_break() would
                // drop it, having nothing to end, so it goes out the way a fenced block's
                // empty line does -- a newline, and no indent to leave behind it.
                if (s[0] == '\0') {
                    out_verbatim(s);
                } else {
                    spans(s);
                    out_break();
                }
                i++;
            }
            out_fill(1);
            continue;
        }

        //
        // A quote's marker is on EVERY line of it, where every other container marks its
        // first line and indents the rest.  Blanking it turns the run into an ordinary
        // two-column indent, which is what the recursion below already knows how to strip
        // -- and, more to the point, what the paragraph join needs: a joined line keeps
        // whatever stands at its own column 0, and an unblanked `>' would land in the text.
        //
        if (has(s, "> ") || strcmp(s, ">") == 0) {
            for (j = i; j < hi; j++) {
                s = at(j, strip);
                if (!has(s, "> ") && strcmp(s, ">") != 0)
                    break;
                s[0] = ' ';
                if (s[1] != '\0')
                    s[1] = ' ';
            }
            out_blank();
            blocks(i, j, strip + 2, col + STEP, depth);
            i = j;
            continue;
        }

        if (has(s, "- ")) {
            container(i, hi, strip, col, 2, "-", depth);
            i = body_end(i + 1, hi, strip, 2);
            continue;
        }

        if (enum_label(s, &w)) {
            j = w - 1; // the label without its trailing space: `1.', `10.'
            if (j > (int)sizeof(label) - 1)
                j = (int)sizeof(label) - 1;
            memcpy(label, s, j);
            label[j] = '\0';
            container(i, hi, strip, col, w, label, depth);
            i = body_end(i + 1, hi, strip, w);
            continue;
        }

        // A term line followed by `: ' is a definition; a `: ' with no term above it keeps
        // the shape and loses nothing, which is ummread.cpp's second Deflist arm.
        if (i + 1 < hi && has(at(i + 1, strip), ": ")) {
            out_blank();
            out_fill(1);
            out_indent(col);
            spans(s);
            out_hang(col + STEP);
            out_nogap();
            end = body_end(i + 2, hi, strip, 2);
            blocks(i + 1, end, strip + 2, col + STEP, depth);
            i = end;
            continue;
        }
        if (has(s, ": ")) {
            container(i, hi, strip, col, 2, 0, depth);
            i = body_end(i + 1, hi, strip, 2);
            continue;
        }

        //
        // Anything else is a filled paragraph.  It always consumes its first line, so a
        // construct this reader does not know cannot spin here.
        //
        // ITS LINES ARE JOINED BEFORE ANY OF THEM IS LOOKED AT, which is ummread.cpp's
        // order and has to be: a bold run crosses a line in ../ls/ls.1.umm and a literal
        // run crosses one in ../../lib/libc/man/getc.3s.umm, and a scanner given one line
        // at a time can close neither.  The join is the NUL between two lines of the group
        // buffer overwritten with a space -- push() lays them down contiguously, so there
        // is nothing to copy and no second buffer.  The extent is settled first: the lines
        // are read as themselves to find where the paragraph ends, and only then joined.
        //
        for (end = i + 1; end < hi; end++) {
            s = at(end, strip);
            if (allblank(s) || starts_block(s))
                break;
            if (end + 1 < hi && has(at(end + 1, strip), ": "))
                break;
        }
        for (j = i; j + 1 < end; j++)
            gline[j][strlen(gline[j])] = ' ';
        out_blank();
        out_fill(1);
        out_indent(col);
        spans(at(i, strip));
        out_break();
        i = end;
    }
}

//
// One page.  The groups are read and rendered one at a time and nothing survives between
// them, which is the property that lets a 4,096-word stack and a 28,672-word address space
// display a manual of any size.
//
int page(FILE *fp, const char *path)
{
    holdful  = 0;
    overflow = 0;
    while (read_group(fp) > 0)
        blocks(0, gn, 0, STEP, 0);
    out_end();
    if (overflow)
        fprintf(stderr, "manview: %s: block too long, output truncated\n", path);
    return overflow ? -1 : 0;
}
