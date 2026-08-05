//
// The roff escape layer, and the cross-reference rule.
//
// Two jobs that both work below the block level.  roff_escapes() turns one line of
// roff text into spans, resolving \f font changes, \(xx special characters and \*x
// strings, and dropping the typesetting escapes a filled terminal renderer has no
// use for.  mark_xrefs() then finds every `name(N)' in the result -- ACROSS span
// boundaries, since roff wrote the idiom as two macros and this format writes it as
// one plain token.
//
// The special-character table is the corpus's, not roff's: only the fourteen forms
// the 200 pages actually used, each mapped in doc/Manual_Page_Format.md section 8.
//
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "intern.h"

namespace umm {

void span_add(std::vector<Span> &out, Font f, const std::string &s)
{
    if (s.empty())
        return;
    if (!out.empty() && out.back().font == f) {
        out.back().text += s;
        return;
    }
    out.push_back({ f, s });
}

std::string span_text(const std::vector<Span> &v)
{
    std::string s;
    for (const Span &sp : v)
        s += sp.text;
    return s;
}

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string squeeze(const std::string &s)
{
    std::string out;
    bool sp = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            sp = true;
            continue;
        }
        if (sp && !out.empty())
            out += ' ';
        sp = false;
        out += c;
    }
    return out;
}

//
// Squeezing has to run across the whole vector rather than span by span, or the
// single space between a bold run and the roman word after it -- which is the last
// character of one span and nothing at all of the next -- would be trimmed away.
//
void squeeze_spans(std::vector<Span> &v)
{
    std::string s;
    std::vector<Font> fonts;
    for (const Span &sp : v) {
        s += sp.text;
        fonts.insert(fonts.end(), sp.text.size(), sp.font);
    }

    std::string keep;
    std::vector<Font> kf;
    bool sp = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            sp = true;
            continue;
        }
        if (c == '\n') { // a line block's own break, which is meaningful
            keep += '\n';
            kf.push_back(fonts[i]);
            sp = false;
            continue;
        }
        if (sp && !keep.empty() && keep.back() != '\n') {
            // A SPACE AT A FONT BOUNDARY IS ROMAN, and this is the model's canonical
            // form rather than a detail.  The writer moves whitespace outside a
            // run's delimiters -- `** bold**' is not bold in any Markdown -- so a
            // space given to either neighbour comes back roman when the dialect is
            // read again, and the round trip would not close.
            keep += ' ';
            kf.push_back(kf.back() == fonts[i] ? fonts[i] : Font::Roman);
        }
        sp = false;
        keep += c;
        kf.push_back(fonts[i]);
    }

    v.clear();
    for (size_t i = 0; i < keep.size(); i++)
        span_add(v, kf[i], keep.substr(i, 1));
}

//
// The fourteen \(xx forms the corpus used.  Two stay ASCII deliberately: \(mi is
// arithmetic prose ("returns -1") where a U+2212 would change text a program's
// output has to match, and \(or is a shell pipe or a grammar alternation, both of
// which are the ASCII character in the thing being described.
//
std::string roff_special(const std::string &name)
{
    static const std::map<std::string, std::string> tab = {
        { "em", "\xE2\x80\x94" }, // em dash
        { "or", "|" },            // NOT U+2223: a pipe or an alternation
        { "mi", "-" },            // NOT U+2212: "returns -1"
        { "bu", "\xE2\x80\xA2" }, // bullet
        { "*p", "\xCF\x80" },     // pi
        { "mu", "\xC3\x97" },     // multiplication sign
        { "pl", "+" },
        { "fm", "\xE2\x80\xB2" }, // prime
        { "+-", "\xC2\xB1" },
        { "**", "*" },
        { "sc", "\xC2\xA7" },
        { "eq", "=" },
        { "bv", "|" },
        { "aa", "\xC2\xB4" },
        { "rq", "\"" },
        { "lq", "\"" },
    };
    auto it = tab.find(name);
    return it == tab.end() ? std::string() : it->second;
}

static Font font_of(char c)
{
    switch (c) {
    case 'B':
        return Font::Bold;
    case 'I':
        return Font::Italic;
    default:
        return Font::Roman; // R and P both close
    }
}

// Skip a quoted escape argument: \w'...', \h'...', \v'...'.  Returns the index
// just past the closing quote, or `i' unchanged if there is no opening one.
static size_t skip_quoted(const std::string &s, size_t i)
{
    if (i >= s.size())
        return i;
    char q = s[i];
    size_t j = s.find(q, i + 1);
    return j == std::string::npos ? s.size() : j + 1;
}

Font roff_escapes(const std::string &in, Font f, std::vector<Span> &out,
                  const std::map<std::string, std::string> &strings, Where w)
{
    std::string pending;
    auto flush = [&]() {
        span_add(out, f, pending);
        pending.clear();
    };
    auto warn = [&](const std::string &m) {
        if (w.diag)
            w.diag->warn(w.path, w.line, m);
    };

    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] != '\\') {
            pending += in[i];
            continue;
        }
        if (++i >= in.size()) // a trailing backslash continues the line in roff
            break;
        char c = in[i];
        switch (c) {
        case 'f': { // font change
            std::string name;
            if (i + 1 < in.size() && in[i + 1] == '(') {
                name = in.substr(i + 2, 2);
                i += 3;
            } else if (i + 1 < in.size()) {
                name = in.substr(i + 1, 1);
                i += 1;
            }
            flush();
            f = font_of(name.empty() ? 'R' : name[0]);
            break;
        }
        case '*': { // string interpolation
            std::string name;
            if (i + 1 < in.size() && in[i + 1] == '(') {
                name = in.substr(i + 2, 2);
                i += 3;
            } else if (i + 1 < in.size()) {
                name = in.substr(i + 1, 1);
                i += 1;
            }
            auto it = strings.find(name);
            if (it == strings.end()) {
                // nroff renders an undefined string as nothing, and two pages rely
                // on that by accident.  Reproduce it, and say so.
                warn("undefined string \\*" + name + " -- rendered as nothing");
                break;
            }
            flush();
            f = roff_escapes(it->second, f, out, strings, w);
            break;
        }
        case '(': { // special character
            std::string name = in.substr(i + 1, 2);
            i += 2;
            std::string rep = roff_special(name);
            if (rep.empty())
                warn("unknown special character \\(" + name);
            pending += rep;
            break;
        }
        case '-':
            pending += '-';
            break;
        case '_': // the underrule, which on a terminal is just an underscore
            pending += '_';
            break;
        case 'e':
            pending += '\\';
            break;
        case ' ':
            pending += ' ';
            break;
        case '\\':
            pending += '\\';
            break;
        case '\'':
            pending += '\'';
            break;
        case '`':
            pending += '`';
            break;
        case '|':
        case '^':
        case '&':
        case '%': // thin space, half space, zero width, hyphenation point
            break;
        case '"': // a comment: the rest of the line is gone
            i = in.size();
            break;
        case 's': { // \s+N, \s-N, \s0 -- a terminal has one type size
            i++;
            if (i < in.size() && (in[i] == '+' || in[i] == '-'))
                i++;
            while (i < in.size() && isdigit((unsigned char)in[i]))
                i++;
            i--;
            break;
        }
        case 'w':
        case 'h':
        case 'v':
            warn(std::string("dropped \\") + c + " -- width or motion, which a filled renderer does not need");
            i = skip_quoted(in, i + 1) - 1;
            break;
        case 'u':
        case 'd':
            warn(std::string("dropped \\") + c +
                 " -- a superscript; write it with ^ (doc/Manual_Page_Format.md section 8)");
            break;
        case 'z':
        case 'c':
            warn(std::string("dropped \\") + c + " -- overstrike or line continuation; rewrite this by hand");
            break;
        default:
            warn(std::string("unhandled escape \\") + c);
            pending += c;
            break;
        }
    }
    flush();
    return f;
}

//
// The cross-reference rule of doc/Manual_Page_Format.md section 3: a name, then
// `(', a section digit, an optional lowercase letter, and `)', with no space.
//
static bool xref_at(const std::string &s, size_t start, size_t &end)
{
    size_t i = start;
    if (i >= s.size() || (!isalpha((unsigned char)s[i]) && s[i] != '_'))
        return false;
    while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '+' ||
                            s[i] == '-'))
        i++;
    if (i == start || i >= s.size() || s[i] != '(')
        return false;
    i++;
    if (i >= s.size() || s[i] < '1' || s[i] > '8')
        return false;
    i++;
    if (i < s.size() && islower((unsigned char)s[i]))
        i++;
    if (i >= s.size() || s[i] != ')')
        return false;
    end = i + 1;
    return true;
}

bool is_xref(const std::string &s)
{
    size_t end = 0;
    return xref_at(s, 0, end) && end == s.size();
}

//
// v7's `word' quoting -> one literal run, the quote characters dropped.
//
// The match is deliberately narrow, because an apostrophe is also an apostrophe:
// the backquote must open at a word boundary, the apostrophe must close at one, no
// backquote or newline may fall between them, and the run has a length cap so that
// an unbalanced quote swallows a phrase rather than a page.  "don't" is not a close
// -- its apostrophe is followed by a letter.
//
void mark_quotes(std::vector<Span> &v)
{
    const size_t CAP = 60;

    std::string s;
    std::vector<Font> fonts;
    for (const Span &sp : v) {
        s += sp.text;
        fonts.insert(fonts.end(), sp.text.size(), sp.font);
    }

    std::string keep;
    std::vector<Font> kf;
    for (size_t i = 0; i < s.size(); i++) {
        bool opens = s[i] == '`' && (i == 0 || !isalnum((unsigned char)s[i - 1])) &&
                     i + 1 < s.size() && !isspace((unsigned char)s[i + 1]);
        size_t close = std::string::npos;
        if (opens) {
            for (size_t j = i + 1; j < s.size() && j - i <= CAP; j++) {
                if (s[j] == '`' || s[j] == '\n')
                    break;
                if (s[j] != '\'')
                    continue;
                if (isspace((unsigned char)s[j - 1])) // ` x ' is not a quotation
                    break;
                if (j + 1 < s.size() && isalnum((unsigned char)s[j + 1]))
                    continue; // an apostrophe inside a word
                close = j;
                break;
            }
        }
        if (close == std::string::npos || close == i + 1) {
            keep += s[i];
            kf.push_back(fonts[i]);
            continue;
        }
        for (size_t j = i + 1; j < close; j++) {
            keep += s[j];
            kf.push_back(Font::Literal);
        }
        i = close;
    }

    v.clear();
    for (size_t i = 0; i < keep.size(); i++)
        span_add(v, kf[i], keep.substr(i, 1));
}

//
// Flatten to text plus a font per byte, retag the matches, and regroup.  Going
// through a per-byte array is what makes a match spanning two spans work, and
// `.IR open (2),' -- italic name, roman parenthesis -- is the common case.
//
void mark_xrefs(std::vector<Span> &v, bool respect_markup)
{
    std::string s;
    std::vector<Font> fonts;
    for (const Span &sp : v) {
        s += sp.text;
        fonts.insert(fonts.end(), sp.text.size(), sp.font);
    }

    bool hit = false;
    for (size_t i = 0; i < s.size(); i++) {
        // A name must start at a word boundary, or `printf' inside `sprintf(3)'
        // would match on its own.
        if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_'))
            continue;
        size_t end = 0;
        if (!xref_at(s, i, end))
            continue;
        bool literal = false, uniform = true;
        for (size_t j = i; j < end; j++) {
            if (fonts[j] == Font::Literal)
                literal = true;
            if (fonts[j] != fonts[i])
                uniform = false;
        }
        if (literal) { // `printf(3)' quoted as itself is not a reference
            i = end - 1;
            continue;
        }
        // Reading the dialect, a match that lies wholly inside one marked run is
        // markup somebody wrote -- `**open(2)**' -- and canonical-shape rule 9 has
        // to be able to see it.  Reading roff there is no such thing: `.IR open (2)'
        // IS the idiom, and every match becomes a reference.
        if (respect_markup && uniform && (fonts[i] == Font::Bold || fonts[i] == Font::Italic)) {
            i = end - 1;
            continue;
        }
        for (size_t j = i; j < end; j++)
            fonts[j] = Font::Xref;
        hit = true;
        i = end - 1;
    }
    if (!hit)
        return;

    std::vector<Span> out;
    for (size_t i = 0; i < s.size(); i++)
        span_add(out, fonts[i], s.substr(i, 1));
    v.swap(out);
}

} // namespace umm
