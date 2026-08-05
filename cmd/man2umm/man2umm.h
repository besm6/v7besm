//
// Unix Manual Markdown: the document model, the two readers, and the writers.
//
// The format is doc/Manual_Page_Format.md and this header is its implementation's
// contract.  Three things share one model: read_man() parses roff -man, read_umm()
// parses the dialect, and write_umm() serializes the dialect back.  That the two
// readers produce the SAME model for the same page is what the round-trip test in
// test/man2umm_test.cpp asserts, and it is the guarantee that a renderer written
// against read_umm() can display everything the converter emits.
//
// run() returns rather than calling exit(), as cmd/nm/nm.h's engine does,
// so the GoogleTest binary can drive it repeatedly in one process.
//
#ifndef MAN2UMM_H
#define MAN2UMM_H

#include <iosfwd>
#include <string>
#include <vector>

namespace umm {

//
// A run of text in one font.  Spans concatenate with no implicit separator: every
// space is a character in some span's text.  The `\&' joiner of the format's rule 3
// is therefore not stored -- it is a serialization artifact write_umm() computes and
// read_umm() discards, which is what lets the round trip close.
//
enum class Font {
    Roman,   // ordinary prose
    Bold,    // **x** -- type it literally
    Italic,  // *x*   -- replace it
    Literal, // `x`   -- quoted as itself
    Xref,    // name(N) -- a cross-reference, written with no markup at all
};

struct Span {
    Font font = Font::Roman;
    std::string text;
};

//
// A block.  Lists and definition lists are one block PER ITEM, the way a Quote is
// one block per display: a run of adjacent Deflist blocks is a definition list.
// That keeps every container the same shape -- something plus `child'.
//
enum class Kind {
    Comment,     // <!-- ... -->            body
    Section,     // ## X                    body
    Subsection,  // ### X                   body
    Para,        // filled text             body
    Lines,       // | one line each         body, '\n' between lines
    Code,        // ``` fenced verbatim     text, info
    Deflist,     // term / : definition     tag, child
    Bullet,      // - item                  child
    Enum,        // 1. item                 child, info = the label
    Quote,       // > display               child
};

struct Block {
    Kind kind = Kind::Para;
    std::string info; // fence info string, or an Enum label
    std::string text; // Code content, verbatim
    std::vector<Span> tag;
    std::vector<Span> body;
    std::vector<Block> child;
};

//
// name/section/extra are the level-1 title: `# LS(1)', `# GETPW(3) [deprecated]'.
//
struct Doc {
    std::string name;
    std::string section;
    std::string extra;
    std::vector<Block> blocks;
};

//
// Diagnostics.  A warning is something the converter dropped that a human should
// look at; an error means the output is not usable as it stands.
//
struct Message {
    bool error = false;
    int line = 0;
    std::string path;
    std::string text;
};

class Diag {
public:
    void warn(const std::string &path, int line, const std::string &text);
    void error(const std::string &path, int line, const std::string &text);

    const std::vector<Message> &messages() const { return msgs; }
    int errors() const { return nerr; }
    int warnings() const { return (int)msgs.size() - nerr; }
    void print(std::ostream &) const;

private:
    std::vector<Message> msgs;
    int nerr = 0;
};

//
// The readers and the writers.  `path' names the input in diagnostics only; for
// read_man() it also roots the .so search, which resolves /usr/include/X onto this
// tree's include/X.
//
Doc read_man(std::istream &, const std::string &path, Diag &);
Doc read_umm(std::istream &, const std::string &path, Diag &);
void write_umm(std::ostream &, const Doc &);
void dump(std::ostream &, const Doc &);

//
// One character of R/B/I/L/X per non-space character of rendered text, in reading
// order.  scripts/mancheck.sh diffs this against the font stream it recovers from
// groff's overstrike, which is the only check that sees whether the fonts survived.
//
void font_stream(std::ostream &, const Doc &);

// Canonical shape, doc/Manual_Page_Format.md section 9.  False if it found an error.
bool lint(const Doc &, const std::string &path, Diag &);

int cli(int argc, char **argv);

} // namespace umm

#endif // MAN2UMM_H
