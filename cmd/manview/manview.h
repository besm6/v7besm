//
// manview -- what the two hundred manual pages of doc/Manual_Page_Format.md look like on a
// terminal.  Task C25a (../README.md).
//
// TWO HALVES AND ONE CONTRACT BETWEEN THEM.  parse.c reads the dialect and calls the
// out_*() routines below in reading order; render.c is those routines and knows nothing
// about the dialect.  There is no document model in between and that is the design --
// ../man2umm/README.md's Doc/Block/Span tree is a host tool's luxury, and §6 of
// ../README.md is why it is not affordable here.  parse.c's header has the measurement.
//
// THE FIVE FONTS ARE THE FORMAT'S, and there are no others: four span kinds (section 2)
// plus the cross-reference (section 3).  F_HEAD and F_SUBHEAD are not span kinds -- no
// page can write one -- but a heading is a run of text that wants an attribute of its own
// and this is the enumeration that carries attributes.
//
#ifndef MANVIEW_H
#define MANVIEW_H

#include <stdio.h>

#define F_ROMAN   0 // ordinary prose
#define F_BOLD    1 // **x**  -- type it literally
#define F_ITALIC  2 // *x*    -- replace it
#define F_LITERAL 3 // `x`    -- quoted as itself; render.c supplies the quotes
#define F_XREF    4 // the NAME of name(N); the parentheses stay roman (section 10)
#define F_HEAD    5 // ## X
#define F_SUBHEAD 6 // ### X
#define NFONT     7

//
// THE INDENT STEP IS FIVE COLUMNS, which is v7's and is what section 10 prescribes.  A
// section heading sits at column 0, a subsection at 3, and everything else at 5 or a
// multiple of it deeper.
//
#define STEP   5
#define SUBCOL 3

// The width of the TERMINAL, not of the text: render.c leaves the rightmost column empty
// and fills to one less than this.
#define DEFWIDTH 80
#define MINWIDTH 20
#define MAXWIDTH 512

// ---- render.c: the output engine ------------------------------------------------------

void out_setup(int width, int plain, int mono);

// The left margin every following line starts at.  Does not break the line.
void out_indent(int col);

//
// FILL MODE IS THE DEFAULT and is what a paragraph wants.  A line block, a heading and a
// fenced block set it off: their line structure is the source's and must not be refilled.
//
void out_fill(int on);

// One run of text in one font.  Spaces inside it are word separators in fill mode and
// ordinary characters otherwise.
void out_span(int font, const char *p, int n);

// End the current output line.  Nothing is emitted if the line is already empty.
void out_break(void);

//
// The hanging indent a definition list is made of: pad out to `col' and continue on this
// same line if the tag left room, break and start there if it did not.
//
void out_hang(int col);

// One blank line.  Consecutive calls collapse, and a call at the top of the output is
// dropped, so no block has to know what came before it.
void out_blank(void);

// The next out_blank() is dropped: what a heading wants of the block under it.
void out_nogap(void);

// One line of verbatim text at the current indent, tabs expanded.  No markup, no fill.
void out_verbatim(const char *s);

// The v7 page banner: name(SECTION) at both margins, `centre' between them.
void out_banner(const char *title, const char *extra);

// Flush anything pending.  Called once per file.
void out_end(void);

// ---- parse.c: the dialect -------------------------------------------------------------

// Render one open stream.  Returns 0, or -1 if the page overflowed the group buffer.
int page(FILE *fp, const char *path);

#endif // MANVIEW_H
