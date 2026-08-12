//
// The dialect reader: Unix Manual Markdown -> model.
//
// This is the half of the tool that OUTLIVES the conversion.  The roff reader has a
// finite job -- the pages that still need converting -- but anything that displays a
// page reads it through here, and the round-trip test
//
//      read_umm(write_umm(read_man(x))) == read_man(x)
//
// is what says the format the converter writes is a format something else can read
// back.  A renderer (cmd/README.md task C25) links this file and adds a back end.
//
// It parses the dialect and only the dialect.  It is not a Markdown implementation:
// there is no link syntax, no HTML, no setext heading, no thematic break, and an
// unrecognized construct is text, not an extension.
//
#include <cctype>
#include <istream>
#include <string>
#include <vector>

#include "intern.h"

namespace umm {
namespace {

bool alnum(char c)
{
    return isalnum((unsigned char)c) != 0;
}

bool blank(const std::string &s)
{
    return trim(s).empty();
}

//
// Inline markup -> spans.  Rule 2's flanking test decides whether a `*' is a
// delimiter at all, which is what lets `2**41-1' through as text: its asterisks
// have a digit on both sides.
//
std::vector<Span> parse_inline(const std::string &s)
{
    std::vector<Span> out;
    std::string pending;
    Font font = Font::Roman;

    auto flush = [&]() {
        span_add(out, font, pending);
        pending.clear();
    };

    for (size_t i = 0; i < s.size();) {
        char c = s[i];

        if (c == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == '&') { // the joiner: a serialization artifact only
                i += 2;
                continue;
            }
            pending += s[i + 1];
            i += 2;
            continue;
        }

        if (c == '`' && font == Font::Roman) {
            // `` x `` is the padded form, for a literal run holding a backtick.
            bool wide = (i + 1 < s.size() && s[i + 1] == '`');
            std::string close = wide ? "``" : "`";
            size_t start = i + close.size();
            size_t end = s.find(close, start);
            if (end == std::string::npos) {
                pending += c;
                i++;
                continue;
            }
            std::string core = s.substr(start, end - start);
            if (wide) {
                if (!core.empty() && core.front() == ' ')
                    core.erase(0, 1);
                if (!core.empty() && core.back() == ' ')
                    core.pop_back();
            }
            flush();
            span_add(out, Font::Literal, core);
            i = end + close.size();
            continue;
        }

        if (c == '*') {
            size_t run = (i + 1 < s.size() && s[i + 1] == '*') ? 2 : 1;
            Font want = run == 2 ? Font::Bold : Font::Italic;

            if (font == want) { // a closing delimiter?
                bool lok = i > 0 && !isspace((unsigned char)s[i - 1]);
                size_t after = i + run;
                bool rok = after >= s.size() || !alnum(s[after]);
                if (lok && rok) {
                    flush();
                    font = Font::Roman;
                    i += run;
                    continue;
                }
            } else if (font == Font::Roman) { // an opening one?
                bool lok = i == 0 || !alnum(s[i - 1]);
                size_t after = i + run;
                bool rok = after < s.size() && !isspace((unsigned char)s[after]);
                if (lok && rok) {
                    flush();
                    font = want;
                    i += run;
                    continue;
                }
            }
            pending += std::string(run, '*');
            i += run;
            continue;
        }

        pending += c;
        i++;
    }
    flush();
    mark_xrefs(out, true);
    return out;
}

//
// Lines that carry `prefix', plus the blank lines between them.  A container's
// body is written first-line-then-indented, so its continuation is found by the
// indent alone.
//
size_t take_indented(const std::vector<std::string> &lines, size_t i, size_t width,
                     std::vector<std::string> &into)
{
    std::string pad(width, ' ');
    while (i < lines.size()) {
        if (blank(lines[i])) {
            // A blank line belongs to the body only if indented text follows.
            size_t j = i;
            while (j < lines.size() && blank(lines[j]))
                j++;
            if (j >= lines.size() || lines[j].compare(0, width, pad) != 0)
                break;
            for (; i < j; i++)
                into.push_back("");
            continue;
        }
        if (lines[i].compare(0, width, pad) != 0)
            break;
        into.push_back(lines[i].substr(width));
        i++;
    }
    return i;
}

// `N. ' at the head of a line: an ordered-list item.
bool enum_label(const std::string &s, std::string &label, size_t &width)
{
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i]))
        i++;
    if (i == 0 || i + 1 >= s.size() || s[i] != '.' || s[i + 1] != ' ')
        return false;
    label = s.substr(0, i + 1);
    width = i + 2;
    return true;
}

// Does this line begin a block other than a paragraph?
bool starts_block(const std::string &s)
{
    std::string label;
    size_t w = 0;
    return s.compare(0, 3, "```") == 0 || s.compare(0, 3, "## ") == 0 ||
           s.compare(0, 4, "### ") == 0 || s.compare(0, 5, "<!-- ") == 0 ||
           s.compare(0, 2, "| ") == 0 || s == "|" || s.compare(0, 2, "> ") == 0 || s == ">" ||
           s.compare(0, 2, "- ") == 0 || s.compare(0, 2, ": ") == 0 || enum_label(s, label, w);
}

std::vector<Block> parse_blocks(const std::vector<std::string> &lines);

// One container item: its first line with the prefix stripped, then its indent.
std::vector<std::string> take_item(const std::vector<std::string> &lines, size_t &i, size_t width)
{
    std::vector<std::string> body;
    body.push_back(lines[i].substr(width));
    i = take_indented(lines, i + 1, width, body);
    while (!body.empty() && blank(body.back()))
        body.pop_back();
    return body;
}

std::vector<Block> parse_blocks(const std::vector<std::string> &lines)
{
    std::vector<Block> out;
    size_t i = 0;

    while (i < lines.size()) {
        if (blank(lines[i])) {
            i++;
            continue;
        }
        const std::string &l = lines[i];

        if (l.compare(0, 3, "```") == 0) {
            Block b;
            b.kind = Kind::Code;
            b.info = trim(l.substr(3));
            i++;
            while (i < lines.size() && lines[i].compare(0, 3, "```") != 0) {
                b.text += lines[i];
                b.text += '\n';
                i++;
            }
            if (i < lines.size())
                i++; // the closing fence
            out.push_back(std::move(b));
            continue;
        }

        if (l.compare(0, 5, "<!-- ") == 0) {
            Block b;
            b.kind = Kind::Comment;
            std::string t = l.substr(5);
            size_t e = t.rfind("-->");
            if (e != std::string::npos)
                t = trim(t.substr(0, e));
            span_add(b.body, Font::Roman, t);
            out.push_back(std::move(b));
            i++;
            continue;
        }

        if (l.compare(0, 4, "### ") == 0) {
            Block b;
            b.kind = Kind::Subsection;
            b.body = parse_inline(trim(l.substr(4)));
            out.push_back(std::move(b));
            i++;
            continue;
        }
        if (l.compare(0, 3, "## ") == 0) {
            Block b;
            b.kind = Kind::Section;
            b.body = parse_inline(trim(l.substr(3)));
            out.push_back(std::move(b));
            i++;
            continue;
        }

        if (l.compare(0, 2, "| ") == 0 || l == "|") {
            Block b;
            b.kind = Kind::Lines;
            bool first = true;
            while (i < lines.size() && (lines[i].compare(0, 2, "| ") == 0 || lines[i] == "|")) {
                if (!first)
                    span_add(b.body, b.body.empty() ? Font::Roman : b.body.back().font, "\n");
                first = false;
                for (const Span &sp : parse_inline(lines[i].size() > 2 ? lines[i].substr(2) : ""))
                    span_add(b.body, sp.font, sp.text);
                i++;
            }
            mark_xrefs(b.body, true);
            out.push_back(std::move(b));
            continue;
        }

        if (l.compare(0, 2, "> ") == 0 || l == ">") {
            std::vector<std::string> body;
            while (i < lines.size() && (lines[i].compare(0, 2, "> ") == 0 || lines[i] == ">")) {
                body.push_back(lines[i].size() > 2 ? lines[i].substr(2) : "");
                i++;
            }
            Block b;
            b.kind = Kind::Quote;
            b.child = parse_blocks(body);
            out.push_back(std::move(b));
            continue;
        }

        if (l.compare(0, 2, "- ") == 0) {
            Block b;
            b.kind = Kind::Bullet;
            b.child = parse_blocks(take_item(lines, i, 2));
            out.push_back(std::move(b));
            continue;
        }

        std::string label;
        size_t width = 0;
        if (enum_label(l, label, width)) {
            Block b;
            b.kind = Kind::Enum;
            b.info = label;
            b.child = parse_blocks(take_item(lines, i, width));
            out.push_back(std::move(b));
            continue;
        }

        // A term line followed by `: ' is a definition.
        if (i + 1 < lines.size() && lines[i + 1].compare(0, 2, ": ") == 0) {
            Block b;
            b.kind = Kind::Deflist;
            b.tag = parse_inline(l);
            i++;
            b.child = parse_blocks(take_item(lines, i, 2));
            out.push_back(std::move(b));
            continue;
        }
        // A definition with no term above it: keep the shape, lose nothing.
        if (l.compare(0, 2, ": ") == 0) {
            Block b;
            b.kind = Kind::Deflist;
            b.child = parse_blocks(take_item(lines, i, 2));
            out.push_back(std::move(b));
            continue;
        }

        // Anything else is a filled paragraph.  It always consumes its first line,
        // so a construct this reader does not know cannot spin here.
        std::string text = l;
        i++;
        while (i < lines.size() && !blank(lines[i]) && !starts_block(lines[i]) &&
               !(i + 1 < lines.size() && lines[i + 1].compare(0, 2, ": ") == 0)) {
            text += ' ';
            text += lines[i];
            i++;
        }
        Block b;
        b.kind = Kind::Para;
        b.body = parse_inline(text);
        out.push_back(std::move(b));
    }
    return out;
}

} // namespace

Doc read_umm(std::istream &in, const std::string &path, Diag &diag)
{
    Doc doc;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }

    size_t i = 0;
    int lineno = 0;

    // The provenance comments, above the title.
    while (i < lines.size()) {
        if (blank(lines[i])) {
            i++;
            lineno++;
            continue;
        }
        if (lines[i].compare(0, 5, "<!-- ") != 0)
            break;
        std::string t = lines[i].substr(5);
        size_t e = t.rfind("-->");
        if (e != std::string::npos)
            t = trim(t.substr(0, e));
        Block b;
        b.kind = Kind::Comment;
        span_add(b.body, Font::Roman, t);
        doc.blocks.push_back(std::move(b));
        i++;
        lineno++;
    }

    if (i >= lines.size() || lines[i].compare(0, 2, "# ") != 0) {
        diag.error(path, lineno + 1, "no level-1 title (doc/Manual_Page_Format.md section 5)");
    } else {
        std::string t = trim(lines[i].substr(2));
        i++;
        size_t lb = t.find(" [");
        if (lb != std::string::npos && t.back() == ']') {
            doc.extra = t.substr(lb + 2, t.size() - lb - 3);
            t.resize(lb);
        }
        size_t op = t.find('(');
        if (op == std::string::npos || t.back() != ')') {
            diag.error(path, lineno + 1, "title is not NAME(SECTION): " + t);
        } else {
            doc.name = t.substr(0, op);
            doc.section = t.substr(op + 1, t.size() - op - 2);
        }
    }

    std::vector<std::string> rest(lines.begin() + (long)i, lines.end());
    for (Block &b : parse_blocks(rest))
        doc.blocks.push_back(std::move(b));
    return doc;
}

} // namespace umm
