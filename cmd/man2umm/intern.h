//
// Internal to cmd/man2umm: what roff.cpp, emit.cpp, ummread.cpp and lint.cpp share
// but nothing outside the tool needs.  The public contract is man2umm.h.
//
#ifndef MAN2UMM_INTERN_H
#define MAN2UMM_INTERN_H

#include <map>
#include <string>
#include <vector>

#include "man2umm.h"

namespace umm {

// Where a diagnostic points.  Threaded through the escape layer, which has no
// other way to say which line of which file dropped a \w'...'.
struct Where {
    Diag *diag = nullptr;
    std::string path;
    int line = 0;
};

// Append `s' in font `f', merging into the previous span when the font matches.
void span_add(std::vector<Span> &out, Font f, const std::string &s);

// The plain text of a span vector: every span's text, concatenated.
std::string span_text(const std::vector<Span> &v);

// Collapse runs of whitespace to one space and trim both ends.  Paragraph text
// is refilled by the renderer, so its internal line structure is not meaningful.
std::string squeeze(const std::string &s);

// The same over a span vector, across span boundaries and preserving the fonts:
// the space between a bold run and the roman word after it belongs to neither.
void squeeze_spans(std::vector<Span> &v);

// Trim ASCII whitespace from both ends.
std::string trim(const std::string &s);

//
// One roff text string -> spans, starting in font `f' and returning the font in
// effect at the end (a \fB may cross a line).  Handles every escape in
// doc/Manual_Page_Format.md section 11, expanding \*x from `strings'.
//
Font roff_escapes(const std::string &in, Font f, std::vector<Span> &out,
                  const std::map<std::string, std::string> &strings, Where);

// The `\(xx' table.  Empty if the name is unknown.
std::string roff_special(const std::string &name);

//
// Mark every `name(N)' in the vector as one Font::Xref span.  Runs across span
// boundaries, which is the point: roff wrote the idiom as `.IR open (2)', two
// spans.  A match that touches a Literal span is left alone.
//
// `respect_markup' is false for the roff reader and true for the dialect reader.
// It decides one case: a match lying wholly inside ONE bold or italic run.  From
// roff that is the ordinary idiom and becomes a reference; from the dialect it is
// `**open(2)**', which canonical-shape rule 9 exists to reject.
//
void mark_xrefs(std::vector<Span> &v, bool respect_markup);

// True if `s' is a cross-reference and nothing else.
bool is_xref(const std::string &s);

//
// v7 quotes a word as itself by writing a backquote, the word, and an apostrophe.
// That is what a Font::Literal run means, so recognize the construct rather than
// transliterating its punctuation: 330 of the corpus's 536 escaped backquotes are
// this and nothing else.  The two quote characters go; the renderer supplies them.
//
// Runs across span boundaries, as mark_xrefs() does, because roff wrote the bold
// flavour as an alternator: `.RB ` x ' is three spans.
//
void mark_quotes(std::vector<Span> &v);

} // namespace umm

#endif // MAN2UMM_INTERN_H
