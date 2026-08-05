//
// The dialect writer: model -> Unix Manual Markdown.
//
// Two things here are subtler than they look.
//
//   THE ESCAPER is governed by rule 2 of doc/Manual_Page_Format.md -- a delimiter
//   counts only when flanked by a non-alphanumeric.  That rule is what lets
//   `2**41-1' and every `time_t' through untouched, and it is also why a `\&' is
//   sometimes needed BETWEEN two spans: `foo**bar**' has its opening ** preceded by
//   a letter and would not be bold at all.  need_joiner() is that test, and it is
//   written to emit the joiner only where it is load-bearing.
//
//   BLOCKS RENDER TO LINES, NOT TO A STREAM.  A definition body, a list item and a
//   quote all indent their contents, and building the lines first means one
//   indenting rule instead of one per container.
//
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "intern.h"

namespace umm {
namespace {

const size_t WRAP = 95;

bool alnum(char c)
{
    return isalnum((unsigned char)c) != 0;
}

// A delimiter character, for the flanking test.
bool delim(char c)
{
    return c == '*' || c == '`';
}

//
// Escape only what rule 2 would otherwise read as markup.  Not `_', not brackets,
// not angle brackets -- keeping those bare is what keeps a raw page readable.
//
// The asterisk test is over the WHOLE RUN, not the character: in `2**41-1' the run
// has a digit before it and a digit after it, so it can neither open nor close and
// needs nothing.  Testing one asterisk at a time would see the other asterisk as
// its non-alphanumeric flank and escape both.
//
std::string escape_md(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '\\') {
            out += "\\\\";
            i++;
            continue;
        }
        if (c == '`') {
            out += "\\`";
            i++;
            continue;
        }
        if (c == '*') {
            size_t run = 0;
            while (i + run < s.size() && s[i + run] == '*')
                run++;
            bool can_open = (i == 0) || !alnum(s[i - 1]);
            bool can_close = (i + run >= s.size()) || !alnum(s[i + run]);
            for (size_t k = 0; k < run; k++) {
                if (can_open || can_close)
                    out += '\\';
                out += '*';
            }
            i += run;
            continue;
        }
        out += c;
        i++;
    }
    return out;
}

std::string open_delim(Font f)
{
    switch (f) {
    case Font::Bold:
        return "**";
    case Font::Italic:
        return "*";
    case Font::Literal:
        return "`";
    default:
        return "";
    }
}

// Is the last character of `s' a backslash escape rather than itself?
bool escaped_tail(const std::string &s)
{
    size_t n = 0;
    for (size_t i = s.size() - 1; i-- > 0;) {
        if (s[i] != '\\')
            break;
        n++;
    }
    return n % 2 == 1;
}

// Is a `\&' needed between these two rendered chunks?
bool need_joiner(const std::string &prev, const std::string &next)
{
    if (prev.empty() || next.empty())
        return false;
    if (escaped_tail(prev)) // a \` is a backtick, not a delimiter
        return false;
    char a = prev.back(), b = next.front();
    if (isspace((unsigned char)a) || isspace((unsigned char)b))
        return false;
    // Two delimiters meeting need it as much as a delimiter meeting a letter:
    // `**b***size*' would be read as a bold run and a stray asterisk.
    return (delim(a) && alnum(b)) || (alnum(a) && delim(b)) || (delim(a) && delim(b));
}

//
// One span vector -> one line of inline markup.  Leading and trailing spaces of a
// marked run move OUTSIDE its delimiters: `** foo **' is not bold in any Markdown.
//
std::string inline_text(const std::vector<Span> &spans)
{
    std::string out;
    for (const Span &sp : spans) {
        if (sp.text.empty())
            continue;

        if (sp.font == Font::Xref) { // written with no markup at all
            if (need_joiner(out, sp.text))
                out += "\\&";
            out += sp.text;
            continue;
        }
        if (sp.font == Font::Roman) {
            std::string t = escape_md(sp.text);
            if (need_joiner(out, t))
                out += "\\&";
            out += t;
            continue;
        }

        size_t b = sp.text.find_first_not_of(" \t");
        if (b == std::string::npos) { // all whitespace: nothing to mark
            out += sp.text;
            continue;
        }
        size_t e = sp.text.find_last_not_of(" \t");
        std::string lead = sp.text.substr(0, b);
        std::string core = sp.text.substr(b, e - b + 1);
        std::string tail = sp.text.substr(e + 1);

        std::string d = open_delim(sp.font);
        std::string chunk;
        if (sp.font == Font::Literal) {
            // A literal run holding a backtick needs a longer fence and a pad.
            if (core.find('`') != std::string::npos)
                chunk = "`` " + core + " ``";
            else
                chunk = "`" + core + "`";
        } else {
            chunk = d + escape_md(core) + d;
        }
        out += lead;
        if (need_joiner(out, chunk))
            out += "\\&";
        out += chunk;
        out += tail;
    }
    return out;
}

// A line that would begin a different block if it started a line.
std::string protect_column1(const std::string &s)
{
    if (s.empty())
        return s;
    static const char *const starts[] = { "#", ">", "- ", "| ", ": ", "+ ", "* " };
    if (std::any_of(std::begin(starts), std::end(starts),
                    [&s](const char *p) { return s.compare(0, strlen(p), p) == 0; }))
        return "\\" + s;
    // `1. ' would begin an ordered list.
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i]))
        i++;
    if (i > 0 && i + 1 < s.size() && s[i] == '.' && s[i + 1] == ' ')
        return s.substr(0, i) + "\\" + s.substr(i);
    return s;
}

// Greedy fill.  Breaking at a space is always safe: a newline is a non-alphanumeric
// on both sides, so rule 2's flanking survives the break.
std::vector<std::string> wrap(const std::string &s, size_t width)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        size_t sp = s.find(' ', i);
        std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        if (!cur.empty() && cur.size() + 1 + word.size() > width) {
            lines.push_back(cur);
            cur.clear();
        }
        if (!cur.empty())
            cur += ' ';
        cur += word;
        if (sp == std::string::npos)
            break;
        i = sp + 1;
    }
    if (!cur.empty())
        lines.push_back(cur);
    if (lines.empty())
        lines.push_back("");
    for (std::string &l : lines)
        l = protect_column1(l);
    return lines;
}

std::vector<std::string> render_blocks(const std::vector<Block> &blocks);

// Prefix the first line with `first' and the rest with `rest', so a definition body
// and a list item indent by one rule rather than by one rule each.
std::vector<std::string> indent(const std::vector<std::string> &in, const std::string &first,
                                const std::string &rest)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < in.size(); i++) {
        const std::string &p = i == 0 ? first : rest;
        if (in[i].empty()) {
            std::string t = p;
            while (!t.empty() && t.back() == ' ')
                t.pop_back();
            out.push_back(t);
        } else {
            out.push_back(p + in[i]);
        }
    }
    return out;
}

std::vector<std::string> render_block(const Block &b)
{
    std::vector<std::string> out;
    switch (b.kind) {
    case Kind::Comment:
        out.push_back("<!-- " + span_text(b.body) + " -->");
        break;

    case Kind::Section:
        out.push_back("## " + inline_text(b.body));
        break;

    case Kind::Subsection:
        out.push_back("### " + inline_text(b.body));
        break;

    case Kind::Para:
        out = wrap(inline_text(b.body), WRAP);
        break;

    case Kind::Lines: {
        // The body's '\n's are the line breaks; markup is live inside each line.
        std::vector<Span> line;
        auto emit_line = [&]() {
            out.push_back("| " + inline_text(line));
            line.clear();
        };
        for (const Span &sp : b.body) {
            size_t start = 0;
            for (;;) {
                size_t nl = sp.text.find('\n', start);
                std::string piece = sp.text.substr(start, nl == std::string::npos ? std::string::npos
                                                                                  : nl - start);
                if (!piece.empty())
                    span_add(line, sp.font, piece);
                if (nl == std::string::npos)
                    break;
                emit_line();
                start = nl + 1;
            }
        }
        if (!line.empty())
            emit_line();
        break;
    }

    case Kind::Code: {
        out.push_back(b.info.empty() ? "```" : "```" + b.info);
        std::string t = b.text;
        size_t start = 0;
        while (start <= t.size()) {
            size_t nl = t.find('\n', start);
            if (nl == std::string::npos) {
                if (start < t.size())
                    out.push_back(t.substr(start));
                break;
            }
            out.push_back(t.substr(start, nl - start));
            start = nl + 1;
        }
        out.push_back("```");
        break;
    }

    case Kind::Deflist:
        out.push_back(inline_text(b.tag));
        {
            std::vector<std::string> body = render_blocks(b.child);
            std::vector<std::string> ind = indent(body, ": ", "  ");
            out.insert(out.end(), ind.begin(), ind.end());
        }
        break;

    case Kind::Bullet: {
        std::vector<std::string> body = render_blocks(b.child);
        out = indent(body, "- ", "  ");
        break;
    }

    case Kind::Enum: {
        std::string label = b.info;
        if (label.empty() || label.back() != '.')
            label += '.';
        std::vector<std::string> body = render_blocks(b.child);
        out = indent(body, label + " ", std::string(label.size() + 1, ' '));
        break;
    }

    case Kind::Quote: {
        std::vector<std::string> body = render_blocks(b.child);
        out = indent(body, "> ", "> ");
        break;
    }
    }
    return out;
}

std::vector<std::string> render_blocks(const std::vector<Block> &blocks)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < blocks.size(); i++) {
        if (i)
            out.push_back("");
        std::vector<std::string> b = render_block(blocks[i]);
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

} // namespace

void write_umm(std::ostream &os, const Doc &doc)
{
    size_t i = 0;
    // The provenance comments come above the title, which is where they were.
    while (i < doc.blocks.size() && doc.blocks[i].kind == Kind::Comment) {
        os << "<!-- " << span_text(doc.blocks[i].body) << " -->\n";
        i++;
    }
    if (i)
        os << '\n';

    os << "# " << doc.name << '(' << doc.section << ')';
    if (!doc.extra.empty())
        os << " [" << doc.extra << ']';
    os << '\n';

    std::vector<Block> rest(doc.blocks.begin() + (long)i, doc.blocks.end());
    os << '\n';
    for (const std::string &l : render_blocks(rest))
        os << l << '\n';
}

//
// The font stream scripts/mancheck.py diffs against groff's overstrike: one letter
// per non-space character of rendered text, in reading order.  The title is not in
// it -- groff turns .TH into a running page header the script strips anyway.
//
namespace {

void fonts_spans(std::ostream &os, const std::vector<Span> &v, char override_to)
{
    for (const Span &sp : v) {
        char c = override_to;
        if (!c) {
            switch (sp.font) {
            case Font::Bold:
                c = 'B';
                break;
            case Font::Italic:
                c = 'I';
                break;
            case Font::Literal:
                c = 'L';
                break;
            case Font::Xref:
                c = 'X';
                break;
            default:
                c = 'R';
                break;
            }
        }
        for (char ch : sp.text)
            if (!isspace((unsigned char)ch))
                os << c;
    }
}

void fonts_blocks(std::ostream &os, const std::vector<Block> &blocks)
{
    for (const Block &b : blocks) {
        switch (b.kind) {
        case Kind::Comment:
            break; // not rendered
        case Kind::Section:
        case Kind::Subsection:
            fonts_spans(os, b.body, 'B'); // roff sets a heading bold
            break;
        case Kind::Para:
        case Kind::Lines:
            fonts_spans(os, b.body, 0);
            break;
        case Kind::Code:
            for (char ch : b.text)
                if (!isspace((unsigned char)ch))
                    os << 'R';
            break;
        case Kind::Deflist:
            fonts_spans(os, b.tag, 0);
            fonts_blocks(os, b.child);
            break;
        case Kind::Enum:
            for (char ch : b.info)
                if (!isspace((unsigned char)ch))
                    os << 'R';
            fonts_blocks(os, b.child);
            break;
        case Kind::Bullet:
        case Kind::Quote:
            fonts_blocks(os, b.child);
            break;
        }
    }
}

const char *kind_name(Kind k)
{
    switch (k) {
    case Kind::Comment:
        return "comment";
    case Kind::Section:
        return "section";
    case Kind::Subsection:
        return "subsection";
    case Kind::Para:
        return "para";
    case Kind::Lines:
        return "lines";
    case Kind::Code:
        return "code";
    case Kind::Deflist:
        return "deflist";
    case Kind::Bullet:
        return "bullet";
    case Kind::Enum:
        return "enum";
    default:
        return "quote";
    }
}

void dump_spans(std::ostream &os, const std::vector<Span> &v, const std::string &pad)
{
    for (const Span &sp : v) {
        const char *f = "roman";
        switch (sp.font) {
        case Font::Bold:
            f = "bold";
            break;
        case Font::Italic:
            f = "italic";
            break;
        case Font::Literal:
            f = "literal";
            break;
        case Font::Xref:
            f = "xref";
            break;
        default:
            break;
        }
        os << pad << f << " [" << sp.text << "]\n";
    }
}

void dump_blocks(std::ostream &os, const std::vector<Block> &blocks, const std::string &pad)
{
    for (const Block &b : blocks) {
        os << pad << kind_name(b.kind);
        if (!b.info.empty())
            os << " info=" << b.info;
        os << '\n';
        if (!b.tag.empty()) {
            os << pad << "  tag:\n";
            dump_spans(os, b.tag, pad + "    ");
        }
        if (!b.body.empty())
            dump_spans(os, b.body, pad + "  ");
        if (!b.text.empty())
            os << pad << "  text [" << b.text << "]\n";
        if (!b.child.empty())
            dump_blocks(os, b.child, pad + "  ");
    }
}

} // namespace

void font_stream(std::ostream &os, const Doc &doc)
{
    fonts_blocks(os, doc.blocks);
    os << '\n';
}

namespace {

// NO SEPARATOR BETWEEN SPANS.  The spaces are already in the text -- squeeze_spans
// put them there -- and a font boundary is not a word boundary: `name=value' is one
// word made of a literal run and the full stop after it.
void words_spans(std::ostream &os, const std::vector<Span> &v)
{
    for (const Span &sp : v)
        os << sp.text;
}

void words_blocks(std::ostream &os, const std::vector<Block> &blocks)
{
    for (const Block &b : blocks) {
        os << ' '; // blocks do not run together, spans do
        switch (b.kind) {
        case Kind::Comment:
            break; // not rendered
        case Kind::Code:
            os << b.text;
            break;
        case Kind::Deflist:
            words_spans(os, b.tag);
            os << ' '; // the term is not the first word of its definition
            words_blocks(os, b.child);
            break;
        case Kind::Enum:
            os << ' ' << b.info; // roff renders the label as text
            words_blocks(os, b.child);
            break;
        case Kind::Bullet: // the marker is not a word on either side
        case Kind::Quote:
            words_blocks(os, b.child);
            break;
        default:
            words_spans(os, b.body);
            break;
        }
    }
}

} // namespace

void word_stream(std::ostream &os, const Doc &doc)
{
    std::ostringstream all;
    words_blocks(all, doc.blocks);
    std::string s = all.str(), word;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!word.empty())
                os << word << '\n';
            word.clear();
            continue;
        }
        word += c;
    }
    if (!word.empty())
        os << word << '\n';
}

void head_stream(std::ostream &os, const Doc &doc)
{
    for (const Block &b : doc.blocks)
        if (b.kind == Kind::Section || b.kind == Kind::Subsection)
            os << squeeze(span_text(b.body)) << '\n';
}

void dump(std::ostream &os, const Doc &doc)
{
    os << "title " << doc.name << '(' << doc.section << ')';
    if (!doc.extra.empty())
        os << " [" << doc.extra << ']';
    os << '\n';
    dump_blocks(os, doc.blocks, "");
}

} // namespace umm
