//
// The roff -man reader.
//
// It handles the subset of requests the 200 pages of this tree actually used, and
// doc/Manual_Page_Format.md section 11 is the complete disposition table -- this
// file implements that table and nothing beyond it.  A request outside it is a
// warning and a dropped line, never a silent one.
//
// Three shapes deserve their names up front:
//
//   THE FRAME STACK.  .RS, .TP and a tagged .IP open a container, and its contents
//   are built in a frame of their own and MOVED into the parent when it closes.
//   Appending to a `child' vector reached through a pointer would work until the
//   parent vector reallocated under it; this cannot.  A frame is `sticky' when only
//   an .RE or a heading may close it -- that is what tells an .RS display, which
//   survives a .PP, from a .TP body, which does not.
//
//   .de AND .ds ARE EXPANDED HERE AND DISCARDED.  The format has no macro facility
//   and must not grow one.  lib/libc/man/intro.2's `.de en' is the corpus's only
//   user macro and its 36 invocations expand mechanically.
//
//   A .nf REGION IS COLLECTED RAW and decided at .fi: a fenced block if nothing in
//   it changed font, a line block if something did.  A fence interprets nothing, so
//   it cannot carry the bold a SYNOPSIS needs; a line block can, and holds the line
//   breaks too.
//
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <istream>
#include <map>
#include <string>
#include <vector>

#include "intern.h"

namespace umm {
namespace {

//
// Split a request's arguments the way roff does: on whitespace, except that a
// double quote opens an argument that runs to the next quote or to end of line.
// The unterminated form is not a mistake -- `.en 1 EPERM "Not owner' is how
// intro.2 writes its errno table.
//
std::vector<std::string> split_args(const std::string &s)
{
    std::vector<std::string> args;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            i++;
        if (i >= s.size())
            break;
        std::string a;
        if (s[i] == '"') {
            i++;
            while (i < s.size()) {
                if (s[i] == '"') {
                    if (i + 1 < s.size() && s[i + 1] == '"') { // "" is one quote
                        a += '"';
                        i += 2;
                        continue;
                    }
                    i++;
                    break;
                }
                a += s[i++];
            }
        } else {
            while (i < s.size() && s[i] != ' ' && s[i] != '\t') {
                // A backslash escape is one unit, so the unpaddable space of
                // `.IR execv \ and \ execl' stays inside its argument.
                if (s[i] == '\\' && i + 1 < s.size()) {
                    a += s[i++];
                    a += s[i++];
                    continue;
                }
                a += s[i++];
            }
        }
        args.push_back(a);
    }
    return args;
}

// Everything after the first whitespace-delimited token, untouched.  .ds needs it:
// the string's text is the rest of the line exactly as written.
std::string after_token(const std::string &s)
{
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        i++;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t')
        i++;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        i++;
    return s.substr(i);
}

// Does this run of verbatim lines look like C?  Only decides a fence's info string.
bool looks_like_c(const std::vector<std::string> &lines)
{
    static const char *const kw[] = { "struct", "union",  "typedef", "#define", "#include",
                                      "int",    "char",   "long",    "unsigned", "void",
                                      "short",  "float",  "double",  "static",  "extern" };
    for (const std::string &l : lines) {
        std::string t = trim(l);
        if (t.empty())
            continue;
        // Only the first substantial line votes.
        return std::any_of(std::begin(kw), std::end(kw),
                           [&t](const char *k) { return t.compare(0, strlen(k), k) == 0; });
    }
    return false;
}

//
// Resolve a .so target onto this tree.  The pages say /usr/include/sys/dir.h and
// mean include/sys/dir.h, so walk up from the page looking for the root -- the
// directory that has include/sys/param.h under it.
//
std::string resolve_include(const std::string &page, const std::string &target)
{
    const std::string want = "/usr/include/";
    if (target.compare(0, want.size(), want) != 0)
        return "";
    std::string rel = "include/" + target.substr(want.size());

    size_t slash = page.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : page.substr(0, slash);
    for (int up = 0; up < 8; up++) {
        std::ifstream probe(dir + "/include/sys/param.h");
        if (probe.good()) {
            std::ifstream hit(dir + "/" + rel);
            return hit.good() ? dir + "/" + rel : std::string();
        }
        dir += "/..";
    }
    return "";
}

//
// A .ta argument, as a column count.  Most of the corpus writes `\w'text'u' width
// functions, which need a typesetter to evaluate and are not worth one: those fall
// back to 8.  The two simple forms are worth having -- `.ta 3i' is curses.3's
// two-column function table, and 3 inches is 30 columns at nroff's 10 per inch.
//
size_t parse_tabstop(const std::string &s)
{
    std::string t = trim(s);
    size_t i = 0;
    while (i < t.size() && isdigit((unsigned char)t[i]))
        i++;
    if (i == 0)
        return 8;
    size_t n = (size_t)std::stoul(t.substr(0, i));
    if (i < t.size() && t[i] == 'i')
        n *= 10; // an inch is ten characters
    return n ? n : 8;
}

// Replace tabs with spaces to the next stop.  A fenced block may hold a tab and a
// line block may not, but neither should depend on how wide the reader thinks one is.
std::string expand_tabs(const std::string &s, size_t stop)
{
    std::string out;
    for (char c : s) {
        if (c != '\t') {
            out += c;
            continue;
        }
        do
            out += ' ';
        while (out.size() % stop != 0);
    }
    return out;
}

// Open parentheses less close ones, so a _Static_assert spanning lines can be
// skipped whole.
int paren_balance(const std::string &s)
{
    return (int)std::count(s.begin(), s.end(), '(') - (int)std::count(s.begin(), s.end(), ')');
}

//
// A header inlined by .so, cut down to the declarations a section 5 page is about:
// no head comment, no include guard, no #include, no _Static_assert.  Dropping them
// is deliberate -- include/sys/dir.h's preamble documents the header, not the file
// format the page is describing.
//
std::string strip_header(std::istream &in)
{
    std::string out, line;
    bool incomment = false, started = false;
    int assert_depth = 0;

    while (std::getline(in, line)) {
        std::string t = trim(line);

        if (assert_depth > 0) { // a _Static_assert continued over lines
            assert_depth += paren_balance(t);
            continue;
        }
        if (incomment) {
            if (t.find("*/") != std::string::npos)
                incomment = false;
            continue;
        }
        if (t.compare(0, 2, "/*") == 0) {
            if (t.find("*/") == std::string::npos)
                incomment = true;
            continue;
        }
        if (t.compare(0, 2, "//") == 0 || t.compare(0, 8, "#include") == 0 ||
            t.compare(0, 7, "#ifndef") == 0 || t.compare(0, 6, "#endif") == 0)
            continue;
        // The guard's own #define: a name and no replacement text.
        if (t.compare(0, 8, "#define ") == 0 && t.find(' ', 8) == std::string::npos)
            continue;
        if (t.compare(0, 14, "_Static_assert") == 0) {
            assert_depth += paren_balance(t);
            continue;
        }
        if (t.empty() && !started)
            continue;
        started = true;
        out += line;
        out += '\n';
    }
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
        out.pop_back();
    return out;
}

//
// SYNOPSIS, which doc/Manual_Page_Format.md section 7 decides by what the section
// holds rather than by what section number the page is.
//
//   A C interface -- every paragraph a whole-line .B, no italic anywhere -- becomes
//   a fenced `c' block.  There are no font distinctions to keep and a declaration
//   should be copy-pasteable.
//
//   A command becomes a line block, because bold and italic are the whole content:
//   bold is what the user types and italic is what the user replaces.  A filled
//   paragraph would keep the fonts and lose the line breaks.
//
void fix_synopsis(Doc &doc)
{
    for (size_t i = 0; i < doc.blocks.size(); i++) {
        if (doc.blocks[i].kind != Kind::Section || trim(span_text(doc.blocks[i].body)) != "SYNOPSIS")
            continue;

        size_t j = i + 1;
        while (j < doc.blocks.size() && doc.blocks[j].kind != Kind::Section)
            j++;
        if (j == i + 1)
            return;

        bool all_bold = true;
        for (size_t k = i + 1; k < j; k++) {
            const Block &b = doc.blocks[k];
            if (b.kind != Kind::Para && b.kind != Kind::Lines) {
                all_bold = false;
                break;
            }
            for (const Span &sp : b.body)
                if (sp.font != Font::Bold && !trim(sp.text).empty())
                    all_bold = false;
        }

        if (all_bold) {
            Block code;
            code.kind = Kind::Code;
            code.info = "c";
            for (size_t k = i + 1; k < j; k++) {
                if (k > i + 1)
                    code.text += '\n';
                code.text += trim(span_text(doc.blocks[k].body));
                code.text += '\n';
            }
            doc.blocks.erase(doc.blocks.begin() + (long)i + 1, doc.blocks.begin() + (long)j);
            doc.blocks.insert(doc.blocks.begin() + (long)i + 1, std::move(code));
        } else {
            for (size_t k = i + 1; k < j; k++)
                if (doc.blocks[k].kind == Kind::Para)
                    doc.blocks[k].kind = Kind::Lines;
        }
        return;
    }
}

//
// The reader.
//
class Reader {
public:
    Reader(const std::string &p, Diag &d) : path(p), diag(d) {}
    Doc run(std::istream &in);

private:
    struct Frame {
        Kind kind = Kind::Quote;
        bool sticky = false; // only .RE or a heading closes it
        std::string info;
        std::vector<Span> tag;
        std::vector<Block> blocks;
    };

    std::string path;
    Diag &diag;
    Doc doc;
    // roff predefines these two, and termcap.3 uses them without defining them.
    std::map<std::string, std::string> strings = { { "lq", "\"" }, { "rq", "\"" } };
    std::map<std::string, std::vector<std::string>> macros;
    std::vector<Frame> stack;
    int lineno = 0;
    int depth = 0; // macro expansion, to stop a macro that calls itself

    std::vector<Span> para;  // the paragraph being accumulated
    // A .br BREAKS ONE LINE, it does not turn the rest of the paragraph into a
    // line block: roff fills whatever follows it until the next break.  sh.1 writes
    // a bold lead-in, a .br, and then six filled lines of prose.
    bool break_next = false;  // the next join is a line break
    bool para_broken = false; // ...and this paragraph has had one, so it is a block
    Font font = Font::Roman;
    bool await_tag = false;      // .TP: the next line is the term
    bool one_line_font = false;  // a bare .B or .I: it lasts one line
    bool no_join = false;        // the line before ended with \c
    bool pending_no_join = false;

    bool nf = false; // a verbatim region
    std::vector<std::string> nfraw;
    size_t tabstop = 8; // the .ta stop in effect; tabs become spaces on the way in
    bool tab_seen = false;

    std::string capturing; // a .de body being captured
    std::vector<std::string> capture;

    Where here() { return { &diag, path, lineno }; }
    void warn(const std::string &m) { diag.warn(path, lineno, m); }
    void err(const std::string &m) { diag.error(path, lineno, m); }
    std::vector<Block> &out() { return stack.back().blocks; }
    void emit(Block b) { out().push_back(std::move(b)); }

    void flush_para();
    void close_frames(bool all);
    void push_frame(Kind k, bool sticky, std::vector<Span> tag, const std::string &info = "");
    void pop_frame();
    void join_para();

    void do_line(const std::string &line);
    void do_request(const std::string &name, const std::string &rest);
    void do_text(const std::string &raw);
    void do_font_macro(const std::string &name, const std::string &rest);
    void do_alternator(const std::string &name, const std::string &rest);
    void do_ip(const std::string &rest);
    void do_so(const std::string &rest);
    void do_comment(const std::string &text);
    bool nf_inline(const std::string &name, const std::string &rest);
    void end_nf();
};

// The separator between what is already in the paragraph and what comes next.
void Reader::join_para()
{
    if (para.empty())
        return;
    if (no_join) { // the line before ended with \c: no space between them
        no_join = false;
        return;
    }
    span_add(para, para.back().font, break_next ? "\n" : " ");
    break_next = false;
}

void Reader::flush_para()
{
    if (para.empty()) {
        break_next = para_broken = false;
        return;
    }
    Block b;
    b.kind = para_broken ? Kind::Lines : Kind::Para;
    b.body = std::move(para);
    para.clear();
    break_next = para_broken = false;
    squeeze_spans(b.body);
    mark_quotes(b.body);
    mark_xrefs(b.body, false);
    if (!b.body.empty())
        emit(std::move(b));
}

void Reader::push_frame(Kind k, bool sticky, std::vector<Span> tag, const std::string &info)
{
    flush_para();
    Frame f;
    f.kind = k;
    f.sticky = sticky;
    f.tag = std::move(tag);
    f.info = info;
    stack.push_back(std::move(f));
}

void Reader::pop_frame()
{
    flush_para();
    Frame f = std::move(stack.back());
    stack.pop_back();
    if (f.blocks.empty() && f.tag.empty())
        return; // an empty .RS/.RE pair says nothing
    Block b;
    b.kind = f.kind;
    b.info = f.info;
    b.tag = std::move(f.tag);
    b.child = std::move(f.blocks);
    emit(std::move(b));
}

// Return to the margin.  `all' also closes an .RS the page forgot to end.
void Reader::close_frames(bool all)
{
    flush_para();
    while (stack.size() > 1) {
        if (!all && stack.back().sticky)
            break;
        pop_frame();
    }
}

void Reader::do_text(const std::string &raw)
{
    // A tab in FILLED text: expand it here, where the .ta stop is known.  Rule 8
    // forbids a tab outside a fence, and refilling cannot keep the columns anyway,
    // so say so once and let a human look at the two pages that do this.
    std::string line = raw;
    // \c at the end of a line continues it: the next line joins with no space.
    if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\\c") == 0) {
        line.resize(line.size() - 2);
        pending_no_join = true;
    }
    if (line.find('\t') != std::string::npos) {
        if (!tab_seen) {
            tab_seen = true;
            warn("a tab in filled text -- this table wants a fenced or a line block");
        }
        line = expand_tabs(line, tabstop);
    }
    if (await_tag) {
        await_tag = false;
        std::vector<Span> tag;
        font = roff_escapes(line, font, tag, strings, here());
        squeeze_spans(tag);
        mark_quotes(tag);
        mark_xrefs(tag, false);
        stack.back().tag = std::move(tag);
        return;
    }
    join_para();
    font = roff_escapes(line, font, para, strings, here());
    no_join = pending_no_join;
    pending_no_join = false;
    if (one_line_font) {
        one_line_font = false;
        font = Font::Roman;
    }
}

void Reader::do_font_macro(const std::string &name, const std::string &rest)
{
    // .SM CHANGES THE SIZE, NOT THE FONT, so it keeps whatever is in effect --
    // sh.1 writes a bare .B above a .SM and expects the word to come out bold.
    // The size itself is nothing on a terminal.
    Font f = font;
    if (name == "B" || name == "SB")
        f = Font::Bold;
    else if (name == "I")
        f = Font::Italic;

    std::vector<std::string> args = split_args(rest);
    if (args.empty()) {
        font = f; // a bare .B takes the next line, and only the next line
        one_line_font = true;
        return;
    }
    std::string joined;
    for (size_t i = 0; i < args.size(); i++) {
        if (i)
            joined += ' ';
        joined += args[i];
    }

    Font save = font;
    if (await_tag) {
        await_tag = false;
        std::vector<Span> tag;
        roff_escapes(joined, f, tag, strings, here());
        mark_quotes(tag);
        mark_xrefs(tag, false);
        stack.back().tag = std::move(tag);
    } else {
        join_para();
        roff_escapes(joined, f, para, strings, here());
    }
    // A bare .B above this one was waiting for a line, and this was the line.
    font = one_line_font ? Font::Roman : save;
    one_line_font = false;
}

//
// .BR .RB .IR .RI .BI .IB -- alternating fonts, JOINED WITH NO SPACE.  That join is
// roff's own rule and it is why `.RB ( \-t )' reads (-t) and not ( -t ).
//
void Reader::do_alternator(const std::string &name, const std::string &rest)
{
    Font a = name[0] == 'B' ? Font::Bold : name[0] == 'I' ? Font::Italic : Font::Roman;
    Font b = name[1] == 'B' ? Font::Bold : name[1] == 'I' ? Font::Italic : Font::Roman;

    std::vector<std::string> args = split_args(rest);
    if (args.empty())
        return;

    std::vector<Span> runs;
    for (size_t i = 0; i < args.size(); i++)
        roff_escapes(args[i], (i % 2) ? b : a, runs, strings, here());

    if (await_tag) {
        await_tag = false;
        mark_quotes(runs);
        mark_xrefs(runs, false);
        stack.back().tag = std::move(runs);
        return;
    }
    join_para();
    for (const Span &sp : runs)
        span_add(para, sp.font, sp.text);
}

//
// .IP is four different things depending on its tag, and the corpus uses all four.
//
void Reader::do_ip(const std::string &rest)
{
    std::vector<std::string> args = split_args(rest);
    std::string tag = args.empty() ? "" : args[0];

    close_frames(false);

    if (tag.empty()) { // an indented display, not a definition
        push_frame(Kind::Quote, false, {});
        return;
    }
    if (tag == "\\(bu") {
        push_frame(Kind::Bullet, false, {});
        return;
    }
    if (isdigit((unsigned char)tag[0])) {
        push_frame(Kind::Enum, false, {}, tag);
        return;
    }
    // THE TAG IS NOT BOLD.  roff sets it in the prevailing font, which is roman
    // unless the page said otherwise -- unlike a .TP tag, which is whatever the
    // line after it makes it.
    std::vector<Span> t;
    roff_escapes(tag, font, t, strings, here());
    mark_quotes(t);
    mark_xrefs(t, false);
    push_frame(Kind::Deflist, false, std::move(t));
}

void Reader::do_so(const std::string &rest)
{
    std::string target = trim(rest);
    std::string file = resolve_include(path, target);
    flush_para();
    if (file.empty()) {
        err("cannot resolve .so " + target);
        Block b;
        b.kind = Kind::Comment;
        span_add(b.body, Font::Roman, "man2umm: FIXME .so " + target);
        emit(std::move(b));
        return;
    }
    std::ifstream in(file);
    Block b;
    b.kind = Kind::Code;
    b.info = "c";
    b.text = strip_header(in);
    emit(std::move(b));
}

void Reader::do_comment(const std::string &text)
{
    // The -Kutf8 notes were advice about a formatter this tree no longer uses.
    // They go; the provenance comments stay.
    if (text.find("Kutf8") != std::string::npos || text.find("nroff") != std::string::npos)
        return;
    flush_para();
    Block b;
    b.kind = Kind::Comment;
    span_add(b.body, Font::Roman, expand_tabs(text, 8));
    emit(std::move(b));
}

//
// A font macro inside .nf.  Rewrite it into the inline \f form so that end_nf()'s
// one font test sees it, and the region's raw lines stay a list of strings.
//
bool Reader::nf_inline(const std::string &name, const std::string &rest)
{
    auto wrap = [](Font f, const std::string &s) {
        switch (f) {
        case Font::Bold:
            return "\\fB" + s + "\\fP";
        case Font::Italic:
            return "\\fI" + s + "\\fP";
        default:
            return s;
        }
    };
    if (name == "B" || name == "I" || name == "SM" || name == "SB") {
        Font f = (name == "B" || name == "SB") ? Font::Bold : name == "I" ? Font::Italic : Font::Roman;
        std::vector<std::string> args = split_args(rest);
        std::string joined;
        for (size_t i = 0; i < args.size(); i++) {
            if (i)
                joined += ' ';
            joined += args[i];
        }
        nfraw.push_back(wrap(f, joined));
        return true;
    }
    if (name == "BR" || name == "RB" || name == "IR" || name == "RI" || name == "BI" || name == "IB") {
        Font a = name[0] == 'B' ? Font::Bold : name[0] == 'I' ? Font::Italic : Font::Roman;
        Font b = name[1] == 'B' ? Font::Bold : name[1] == 'I' ? Font::Italic : Font::Roman;
        std::vector<std::string> args = split_args(rest);
        std::string joined;
        for (size_t i = 0; i < args.size(); i++)
            joined += wrap((i % 2) ? b : a, args[i]);
        nfraw.push_back(joined);
        return true;
    }
    return false;
}

void Reader::end_nf()
{
    nf = false;
    while (!nfraw.empty() && trim(nfraw.back()).empty())
        nfraw.pop_back();
    for (std::string &l : nfraw)
        l = expand_tabs(l, tabstop);
    if (nfraw.empty())
        return;

    bool fonts = false;
    for (const std::string &l : nfraw)
        if (l.find("\\f") != std::string::npos)
            fonts = true;

    Block b;
    if (!fonts) {
        b.kind = Kind::Code;
        b.info = looks_like_c(nfraw) ? "c" : "";
        for (const std::string &l : nfraw) {
            // Only the escapes that mean a character: markup would be a lie inside
            // a fence, where nothing is interpreted.
            std::vector<Span> tmp;
            roff_escapes(l, Font::Roman, tmp, strings, here());
            b.text += span_text(tmp);
            b.text += '\n';
        }
    } else {
        b.kind = Kind::Lines;
        Font f = Font::Roman;
        for (size_t i = 0; i < nfraw.size(); i++) {
            if (i)
                span_add(b.body, b.body.empty() ? Font::Roman : b.body.back().font, "\n");
            f = roff_escapes(nfraw[i], f, b.body, strings, here());
        }
        mark_quotes(b.body);
        mark_xrefs(b.body, false);
    }
    nfraw.clear();
    emit(std::move(b));
}

void Reader::do_request(const std::string &name, const std::string &rest)
{
    // --- the page title -------------------------------------------------------
    if (name == "TH") {
        std::vector<std::string> a = split_args(rest);
        if (a.size() < 2) {
            err(".TH needs a name and a section");
            return;
        }
        doc.name = a[0];
        doc.section = a[1];
        // curses.3 and termcap.3 say 3X and live in files called .3.  The title's
        // section must match the filename (canonical shape rule 2), so the X goes.
        if (doc.section.size() == 2 && toupper((unsigned char)doc.section[1]) == 'X' &&
            path.size() >= 2 && path.compare(path.size() - 2, 2, ".3") == 0)
            doc.section.resize(1);
        if (a.size() > 2)
            doc.extra = a[2];
        return;
    }

    // --- headings -------------------------------------------------------------
    if (name == "SH" || name == "SS") {
        close_frames(true);
        std::vector<std::string> a = split_args(rest);
        std::string t;
        for (size_t i = 0; i < a.size(); i++) {
            if (i)
                t += ' ';
            t += a[i];
        }
        Block b;
        b.kind = name == "SH" ? Kind::Section : Kind::Subsection;
        roff_escapes(t, Font::Roman, b.body, strings, here());
        mark_quotes(b.body);
        mark_xrefs(b.body, false);
        font = Font::Roman;
        emit(std::move(b));
        return;
    }

    // --- paragraphs and containers --------------------------------------------
    if (name == "PP" || name == "LP" || name == "P") {
        close_frames(false);
        font = Font::Roman;
        return;
    }
    if (name == "br") {
        if (!para.empty())
            break_next = para_broken = true;
        return;
    }
    if (name == "TP") {
        close_frames(false);
        push_frame(Kind::Deflist, false, {});
        await_tag = true;
        return;
    }
    if (name == "IP") {
        do_ip(rest);
        return;
    }
    if (name == "HP") {
        warn(".HP is a hanging paragraph -- rewrite it as a definition list");
        close_frames(false);
        break_next = para_broken = true;
        return;
    }
    if (name == "RS") {
        push_frame(Kind::Quote, true, {});
        return;
    }
    if (name == "RE") {
        close_frames(false);
        if (stack.size() < 2) {
            err(".RE with no .RS");
            return;
        }
        pop_frame();
        return;
    }

    // --- fonts ----------------------------------------------------------------
    if (name == "B" || name == "I" || name == "SM" || name == "SB") {
        do_font_macro(name, rest);
        return;
    }
    if (name == "BR" || name == "RB" || name == "IR" || name == "RI" || name == "BI" || name == "IB") {
        do_alternator(name, rest);
        return;
    }

    // --- verbatim -------------------------------------------------------------
    if (name == "nf") {
        flush_para();
        nf = true;
        return;
    }
    if (name == "fi") {
        end_nf();
        return;
    }

    // --- macros and strings, expanded here and discarded ----------------------
    if (name == "ds") {
        std::vector<std::string> a = split_args(rest);
        if (!a.empty())
            strings[a[0]] = after_token(rest);
        return;
    }
    if (name == "de") {
        std::vector<std::string> a = split_args(rest);
        if (a.empty())
            return;
        capturing = a[0];
        capture.clear();
        return;
    }
    if (name == "so") {
        do_so(rest);
        return;
    }

    // --- conditionals: the target is a terminal --------------------------------
    if (name == "if" || name == "ie" || name == "el") {
        std::string r = trim(rest);
        if (r.find("\\{") != std::string::npos) {
            warn("dropped a multi-line ." + name + " -- check this page by hand");
            return;
        }
        if (name == "if" && r.compare(0, 2, "n ") == 0) {
            do_line(trim(r.substr(2)));
            return;
        }
        if (name == "if" && r.compare(0, 2, "t ") == 0)
            return; // the typesetter branch, which this format has no use for
        warn("dropped ." + name + ' ' + r);
        return;
    }

    // --- requests with no meaning to a filled renderer -------------------------
    if (name == "ta") {
        tabstop = parse_tabstop(rest);
        return;
    }
    static const char *const dropped[] = { "ns", "PD", "DT", "ne", "dt", "tr", "nh",
                                           "hy", "ft", "ce", "ad", "pg", "ti", "in", "UC",
                                           "sp", "na", "bp", "nr", "rm", "ll", "po" };
    if (std::any_of(std::begin(dropped), std::end(dropped),
                    [&name](const char *d) { return name == d; }))
        return;

    // --- a user macro ----------------------------------------------------------
    auto m = macros.find(name);
    if (m != macros.end()) {
        if (depth > 8) {
            err("macro ." + name + " nests too deeply");
            return;
        }
        std::vector<std::string> a = split_args(rest);
        std::vector<std::string> body = m->second;
        depth++;
        for (std::string bl : body) {
            for (size_t i = 0; i < 9; i++) {
                std::string ref = "\\$" + std::to_string(i + 1);
                std::string val = i < a.size() ? a[i] : "";
                size_t p;
                while ((p = bl.find(ref)) != std::string::npos)
                    bl.replace(p, ref.size(), val);
            }
            do_line(bl);
        }
        depth--;
        return;
    }

    warn("unhandled request ." + name);
}

void Reader::do_line(const std::string &line)
{
    // Capturing a .de body?  Everything up to `..' is the macro, verbatim, with the
    // doubled backslash of an argument reference halved.
    if (!capturing.empty()) {
        if (trim(line) == "..") {
            macros[capturing] = capture;
            capturing.clear();
            capture.clear();
            return;
        }
        std::string body = line;
        size_t p;
        while ((p = body.find("\\\\$")) != std::string::npos)
            body.replace(p, 3, "\\$");
        capture.push_back(body);
        return;
    }

    if (line.empty()) { // a blank line breaks a paragraph in roff too
        if (nf)
            nfraw.push_back("");
        else
            close_frames(false);
        return;
    }

    if (line[0] != '.' && line[0] != '\'') {
        if (nf)
            nfraw.push_back(line);
        else
            do_text(line);
        return;
    }

    size_t i = 1;
    std::string name;
    while (i < line.size() && !isspace((unsigned char)line[i]))
        name += line[i++];
    while (i < line.size() && isspace((unsigned char)line[i]))
        i++;
    std::string rest = line.substr(i);

    if (name.compare(0, 2, "\\\"") == 0) {
        do_comment(trim(line.size() > 3 ? line.substr(3) : ""));
        return;
    }
    if (nf) {
        if (name == "fi") {
            end_nf();
            return;
        }
        if (name == "nf")
            return;
        if (name == "SH" || name == "SS") {
            // A heading always returns to the margin, so it ends a verbatim region
            // -- curses.3 opens one for its function table and never closes it.
            end_nf();
            do_request(name, rest);
            return;
        }
        if (name == "ds" || name == "de") { // a definition, not output
            do_request(name, rest);
            return;
        }
        if (name == "so") { // the five section 5 pages put theirs inside .nf
            end_nf();
            nf = true;
            do_so(rest);
            return;
        }
        if (name == "PP" || name == "LP" || name == "sp") {
            nfraw.push_back("");
            return;
        }
        if (name == "br") // every line already breaks in a verbatim region
            return;
        if (name == "ta") {
            tabstop = parse_tabstop(rest);
            return;
        }
        if (name == "ne" || name == "in" || name == "ti" || name == "ad" || name == "na" ||
            name == "ce" || name == "ft" || name == "ns" || name == "PD" || name == "nh" ||
            name == "hy")
            return; // typography a fenced block has no use for
        if (nf_inline(name, rest))
            return;
        warn("dropped ." + name + " inside .nf");
        return;
    }
    do_request(name, rest);
}

Doc Reader::run(std::istream &in)
{
    stack.emplace_back(); // the document frame

    std::string line;
    while (std::getline(in, line)) {
        lineno++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        do_line(line);
    }
    if (nf)
        end_nf();
    close_frames(true);

    doc.blocks = std::move(stack.front().blocks);
    if (doc.name.empty())
        err("no .TH: the page has no title");
    fix_synopsis(doc);
    return doc;
}

} // namespace

Doc read_man(std::istream &in, const std::string &path, Diag &diag)
{
    Reader r(path, diag);
    return r.run(in);
}

} // namespace umm
