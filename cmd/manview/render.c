//
// The output engine: the half of manview that knows about a terminal and nothing about
// the dialect.  parse.c calls these in reading order and this file decides where a line
// ends, how far it is indented, and what escape sequence carries a font.
//
// SECTION 10 of ../../doc/Manual_Page_Format.md is the specification, and the four rules
// that shape this file are its: FILL a paragraph and nothing else, INDENT by a fixed step
// of five, QUOTE a literal run because the source holds only the delimiters, and NEVER
// HYPHENATE -- a manual page is full of tokens that must not break, so a word longer than
// the measure goes out over the margin rather than in two pieces.
//
// A COLUMN IS A CHARACTER, NOT A BYTE.  Bytes 0200..0277 are UTF-8 continuations and take
// no column of their own; without that the seventeen pages of this corpus with Cyrillic in
// them fold at half the width.  ../more/more.c's get_line() has the same clause for the
// same reason and cmd/novi's cont() had it first.  Emitting still walks bytes, which is
// correct: it emits, it does not measure.
//
// ATTRIBUTES ARE OPENED AND CLOSED AROUND EVERY RUN rather than tracked across the output.
// It costs a few bytes on a line that alternates fonts and it buys the property that
// matters on a machine whose pager is ../more: no escape sequence outlives the run it
// belongs to, so nothing can leave a terminal in an attribute, and the output cut at any
// line boundary leaves both halves well formed.
//
#include <stdio.h>
#include <string.h>

#include "manview.h"

//
// A PENDING WORD, and it may cross fonts: `**b**\&*size*' is one word by the joiner of
// rule 3, and `open(2)' is one by construction.  The fill decision is taken on the whole
// of it, so the buffer holds the bytes and the run boundaries until a space arrives.
//
#define WORDSZ   512 // bytes of one word -- the corpus's longest is under a tenth of it
#define WORDRUNS 32  // font changes within one word

struct run {
    int font;
    int len;
};

static int width = DEFWIDTH;
static int plain; // -p: no escape sequences at all
static int mono;  // -m: attributes, no colour

static int indent;      // the left margin of the lines being emitted now
static int curcol;      // display columns already on this line
static int linestarted; // ... and whether its indent has been written
static int linehas;     // ... and whether anything but the indent is on it
static int filling = 1;
static int pendgap; // a blank line is owed before the next line begins
static int started; // anything at all has been emitted
static int nogap;   // ... and the next out_blank() is to be dropped

static char wbuf[WORDSZ];
static struct run wrun[WORDRUNS];
static int wnrun;
static int wlen;
static int wwidth;

//
// The attribute table.  Section 10 asks for the name of a cross-reference in italic with
// roman parentheses and leaves colour to the renderer; these are the seven answers.  Bold,
// italic and underline are the three SGR attributes a v7 page can actually mean -- what
// you type, what you replace, and what is quoted as itself.
//
static const char *const sgr[NFONT] = {
    "",           // roman
    "\033[1;32m", // bold: what the user types
    "\033[3;36m", // italic: what the user replaces
    "\033[4m",    // literal: quoted as itself
    "\033[3;35m", // cross-reference name
    "\033[1;33m", // ## section heading
    "\033[1;36m", // ### subsection heading
};

//
// -m, for a terminal that does bold and not colour.  ../more/README.md's position is that
// a terminal which is not ANSI cannot use the pager either; this is the half-way house
// between that and giving up on attributes altogether.
//
static const char *const sgr_mono[NFONT] = {
    "", "\033[1m", "\033[3m", "\033[4m", "\033[3m", "\033[1m", "\033[1m",
};

void out_setup(int w, int p, int m)
{
    // ONE COLUMN OF RIGHT PADDING.  A line that ends in the terminal's last column leaves
    // the cursor in the pending-wrap position, and the next newline reads as a fold; the
    // caller says how wide the terminal is and text gets one column less than that.
    width = w - 1;
    plain = p;
    mono  = m;
}

// Display columns in n bytes: a UTF-8 continuation byte is part of the character before it.
static int cols(const char *p, int n)
{
    int i, w;

    w = 0;
    for (i = 0; i < n; i++)
        if (((unsigned char)p[i] & 0300) != 0200)
            w++;
    return w;
}

static void attr_on(int font)
{
    if (plain || font == F_ROMAN)
        return;
    fputs(mono ? sgr_mono[font] : sgr[font], stdout);
}

static void attr_off(int font)
{
    if (plain || font == F_ROMAN)
        return;
    fputs("\033[0m", stdout);
}

// Begin a line: the blank line it owes, then its indent.  curcol is meaningful afterwards.
static void startline(void)
{
    int i;

    if (linestarted)
        return;
    if (pendgap) {
        putchar('\n');
        pendgap = 0;
    }
    for (i = 0; i < indent; i++)
        putchar(' ');
    curcol      = indent;
    linestarted = 1;
    linehas     = 0;
    started     = 1;
}

static void endline(void)
{
    if (!linestarted)
        return;
    putchar('\n');
    curcol      = 0;
    linestarted = 0;
    linehas     = 0;
}

// Put the pending word on the page, wherever the fill rule says it goes.
static void wflush(void)
{
    const char *p;
    int i;

    if (wlen == 0)
        return;
    if (linehas) {
        if (curcol + 1 + wwidth <= width) {
            putchar(' ');
            curcol++;
        } else {
            endline();
        }
    }
    startline();
    p = wbuf;
    for (i = 0; i < wnrun; i++) {
        attr_on(wrun[i].font);
        fwrite(p, 1, wrun[i].len, stdout);
        attr_off(wrun[i].font);
        p += wrun[i].len;
    }
    curcol += wwidth;
    linehas = 1;
    wlen = wwidth = wnrun = 0;
}

// Add bytes to the pending word, merging into its last run when the font matches.
static void wadd(int font, const char *p, int n)
{
    if (n <= 0)
        return;
    // A word past either bound cannot happen in a page anybody reads, and if it does the
    // answer is to emit what there is: losing it would be worse and hyphenating is refused.
    if (wlen + n > WORDSZ || wnrun >= WORDRUNS)
        wflush();
    if (n > WORDSZ)
        n = WORDSZ;
    memcpy(wbuf + wlen, p, n);
    if (wnrun > 0 && wrun[wnrun - 1].font == font) {
        wrun[wnrun - 1].len += n;
    } else {
        wrun[wnrun].font = font;
        wrun[wnrun].len  = n;
        wnrun++;
    }
    wlen += n;
    wwidth += cols(p, n);
}

// Text straight onto the line, no word buffer: what a line block and a heading want.
static void raw(int font, const char *p, int n)
{
    if (n <= 0)
        return;
    startline();
    attr_on(font);
    fwrite(p, 1, n, stdout);
    attr_off(font);
    curcol += cols(p, n);
    linehas = 1;
}

static void span1(int font, const char *p, int n)
{
    int i, j;

    if (!filling) {
        raw(font, p, n);
        return;
    }
    for (i = 0; i < n;) {
        if (p[i] == ' ' || p[i] == '\t') {
            wflush();
            i++;
            continue;
        }
        for (j = i; j < n && p[j] != ' ' && p[j] != '\t'; j++)
            ;
        wadd(font, p + i, j - i);
        i = j;
    }
}

//
// A LITERAL RUN IS THREE PIECES, and the two quotes are this file's rather than the
// source's -- section 2 says so, and it is why the format stores the delimiters alone.
// v7 wrote a backquote and an apostrophe and so does this.  The quotes are roman: they are
// punctuation the renderer added, not part of what is being quoted.
//
void out_span(int font, const char *p, int n)
{
    if (font != F_LITERAL) {
        span1(font, p, n);
        return;
    }
    span1(F_ROMAN, "`", 1);
    span1(F_LITERAL, p, n);
    span1(F_ROMAN, "'", 1);
}

void out_indent(int col)
{
    indent = col;
}

void out_fill(int on)
{
    if (!on)
        wflush();
    filling = on;
}

void out_break(void)
{
    wflush();
    endline();
}

//
// The hanging indent of a definition list.  A tag that left room keeps the body on its own
// line -- the shape v7's .TP has always had -- and one that did not gets the next line.
// Clearing linehas is what stops the fill rule putting a second space after the padding.
//
void out_hang(int col)
{
    wflush();
    indent = col;
    if (linestarted && curcol < col) {
        while (curcol < col) {
            putchar(' ');
            curcol++;
        }
        linehas = 0;
        return;
    }
    endline();
}

//
// The separator between two blocks -- and the one call that a hanging tag must be able to
// swallow whole, line and all.  Every block begins with out_blank() and ends having closed
// its own line, so a suppressed call here is what lets a definition's body go on the tag's
// line rather than under it.
//
void out_blank(void)
{
    if (nogap) {
        nogap = 0;
        return;
    }
    out_break();
    if (started)
        pendgap = 1;
}

void out_nogap(void)
{
    nogap = 1;
}

//
// Verbatim, at the current indent, with tabs to the next eight-column stop.  A fenced block
// is the one construct where a tab is permitted (section 8) and the one whose columns must
// survive, which is the whole reason it exists.
//
// THE STOPS ARE COUNTED FROM THE BLOCK'S OWN COLUMN 0 AND NOT THE PAGE'S, which is the only
// way the second half of that sentence can be true: the indent is five and a tab stop is
// eight, so measuring from the page would shift every column of a hand-aligned table by a
// different amount.  Measuring from the block shifts the whole of it by five.
//
void out_verbatim(const char *s)
{
    int i, j;

    // An empty line inside a fence carries no indent: five spaces and a newline is
    // trailing whitespace, and a page full of it is a page nothing will cmp cleanly.
    if (s[0] == '\0') {
        if (pendgap) {
            putchar('\n');
            pendgap = 0;
        }
        putchar('\n');
        started = 1;
        return;
    }
    startline();
    for (i = 0; s[i] != '\0';) {
        if (s[i] == '\t') {
            do {
                putchar(' ');
                curcol++;
            } while ((curcol - indent) & 7);
            i++;
            linehas = 1;
            continue;
        }
        for (j = i; s[j] != '\0' && s[j] != '\t'; j++)
            ;
        fwrite(s + i, 1, j - i, stdout);
        curcol += cols(s + i, j - i);
        linehas = 1;
        i       = j;
    }
    endline();
}

//
// The v7 banner, from the level-1 title of section 5: the page's own name at both margins
// with the manual's name between them.  There is no footer -- more(1) does the paging here
// and there is no page number to put in one -- and no attempt to fit a title that will not:
// a name wider than the measure gets the left field alone.
//
// The bracketed qualifier of section 5 goes on a centred second line.  Five pages carry
// one and it is the only place in the format where a title says something extra.
//
void out_banner(const char *title, const char *extra)
{
    static const char centre[] = "UNIX Programmer's Manual";
    int lw, cw, pad, i;

    lw = (int)strlen(title);
    cw = (int)sizeof(centre) - 1;

    out_fill(0);
    out_indent(0);
    raw(F_HEAD, title, lw);
    if (2 * lw + cw + 2 <= width) {
        pad = (width - 2 * lw - cw) / 2;
        for (i = 0; i < pad; i++)
            putchar(' ');
        curcol += pad;
        raw(F_HEAD, centre, cw);
        pad = width - curcol - lw;
        for (i = 0; i < pad; i++)
            putchar(' ');
        curcol += pad;
        raw(F_HEAD, title, lw);
    }
    endline();

    if (extra != 0 && extra[0] != '\0') {
        lw  = (int)strlen(extra);
        pad = (width - cols(extra, lw)) / 2;
        if (pad < 0)
            pad = 0;
        out_indent(pad);
        raw(F_ROMAN, extra, lw);
        endline();
    }
    out_fill(1);
    out_indent(STEP);
    out_blank();
}

// One page done.  `started' is deliberately left set: manview.c puts a blank line between
// two pages, and man -a hands this program both of them in one command.
void out_end(void)
{
    out_break();
    pendgap = 0;
    nogap   = 0;
}
