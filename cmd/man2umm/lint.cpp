//
// Canonical shape: the ten rules of doc/Manual_Page_Format.md section 9.
//
// This is the check that OUTLIVES the conversion.  The roff sources go, and with
// them scripts/mancheck.py's ability to compare a page against groff; what is left
// to hold a hand-edited page to the format is this, registered as one ctest per
// page.  So the rules here are the ones a reader or an index depends on, not
// house style: a NAME line an index can parse, a title that matches the filename,
// a cross-reference a renderer can still recognize.
//
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "intern.h"

namespace umm {
namespace {

// The page's own name and section, from its path: cmd/ls/ls.1.umm -> ls, 1.
void split_path(const std::string &path, std::string &stem, std::string &section)
{
    std::string base = path;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".umm") == 0)
        base.resize(base.size() - 4);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos) {
        stem = base;
        return;
    }
    stem = base.substr(0, dot);
    section = base.substr(dot + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return s;
}

// The conventional section vocabulary.  Outside it is a warning, never an error --
// this port's prose headings (## HOW MANY FILES, ## BUFFERING) are deliberate.
const std::set<std::string> &known_sections()
{
    static const std::set<std::string> s = {
        "NAME",        "SYNOPSIS", "DESCRIPTION", "OPTIONS",   "FILES", "DIAGNOSTICS",
        "SEE ALSO",    "BUGS",     "AUTHOR",      "ASSEMBLER", "NOTES", "BESM-6 NOTES",
        "EXAMPLES",    "LIMITS",   "ENVIRONMENT", "EXIT STATUS",
    };
    return s;
}

void walk(const std::vector<Block> &blocks, const std::string &path, Diag &diag, bool &tabs,
          bool &fixme);

void check_spans(const std::vector<Span> &v, const std::string &path, Diag &diag, bool &tabs,
                 bool &fixme)
{
    for (const Span &sp : v) {
        if (sp.text.find('\t') != std::string::npos)
            tabs = true;
        if (sp.text.find("man2umm: FIXME") != std::string::npos)
            fixme = true;
        // Rule 9: a cross-reference must be plain, or a renderer cannot see it.
        if (sp.font == Font::Bold || sp.font == Font::Italic) {
            std::string t = trim(sp.text);
            if (is_xref(t))
                diag.error(path, 0, "cross-reference " + t + " carries inline markup (rule 9)");
        }
    }
}

void walk(const std::vector<Block> &blocks, const std::string &path, Diag &diag, bool &tabs,
          bool &fixme)
{
    for (const Block &b : blocks) {
        check_spans(b.tag, path, diag, tabs, fixme);
        if (b.kind != Kind::Code)
            check_spans(b.body, path, diag, tabs, fixme);
        if (b.kind == Kind::Comment && span_text(b.body).find("man2umm: FIXME") != std::string::npos)
            fixme = true;
        walk(b.child, path, diag, tabs, fixme);
    }
}

} // namespace

bool lint(const Doc &doc, const std::string &path, Diag &diag)
{
    int before = diag.errors();

    // Rules 1 and 2: one title, matching the filename.
    std::string stem, section;
    split_path(path, stem, section);
    if (doc.name.empty())
        diag.error(path, 0, "no page title (rule 1)");
    else if (lower(doc.name) != lower(stem))
        diag.error(path, 0, "title names " + doc.name + " but the file is " + stem + " (rule 2)");
    if (!section.empty() && lower(doc.section) != lower(section))
        diag.error(path, 0, "title section (" + doc.section + ") does not match the file's ." +
                                section + " (rule 2)");

    // Rules 3 and 4: NAME first and in shape, DESCRIPTION present.
    const Block *first = nullptr;
    bool have_desc = false, skipped = false;
    int last_level = 2;
    for (const Block &b : doc.blocks) {
        if (b.kind == Kind::Section) {
            std::string h = trim(span_text(b.body));
            if (!first)
                first = &b;
            if (h == "DESCRIPTION")
                have_desc = true;
            if (!known_sections().count(h))
                diag.warn(path, 0, "unconventional section heading: " + h);
            last_level = 2;
        } else if (b.kind == Kind::Subsection) {
            if (last_level < 2)
                skipped = true;
            last_level = 3;
        }
    }
    if (!first)
        diag.error(path, 0, "no sections at all (rule 3)");
    else if (trim(span_text(first->body)) != "NAME")
        diag.error(path, 0, "the first section is " + trim(span_text(first->body)) +
                                ", not NAME (rule 3)");
    if (!have_desc)
        diag.error(path, 0, "no DESCRIPTION section (rule 4)");
    if (skipped)
        diag.error(path, 0, "a heading level is skipped (rule 5)");

    // Rule 3, the NAME line itself: `name[, name]... - one-line description'.
    for (size_t i = 0; i < doc.blocks.size(); i++) {
        if (doc.blocks[i].kind != Kind::Section || trim(span_text(doc.blocks[i].body)) != "NAME")
            continue;
        if (i + 1 >= doc.blocks.size() || doc.blocks[i + 1].kind != Kind::Para) {
            diag.error(path, 0, "NAME has no one-paragraph body (rule 3)");
            break;
        }
        std::string t = trim(span_text(doc.blocks[i + 1].body));
        size_t dash = t.find(" - ");
        if (dash == std::string::npos) {
            diag.error(path, 0, "NAME is not `name... - description' (rule 3): " + t);
            break;
        }
        // The page's own name SHOULD be among those listed, but a group page is
        // named for its topic and need not be: exec(2) lists execl, execv, execle
        // and does not list `exec' at all.  So this is a warning, not an error.
        std::string names = lower(t.substr(0, dash)) + ",";
        bool listed = names.find(lower(doc.name) + ",") != std::string::npos ||
                      names.find(lower(doc.name) + " ") != std::string::npos;
        if (!doc.name.empty() && !listed)
            diag.warn(path, 0, "NAME does not list " + doc.name + " itself");
        break;
    }

    // Rules 6, 8 and 9, which are properties of the text rather than the outline.
    bool tabs = false, fixme = false;
    walk(doc.blocks, path, diag, tabs, fixme);
    if (fixme)
        diag.error(path, 0, "a `man2umm: FIXME' marker survives (rule 6)");
    if (tabs)
        diag.error(path, 0, "a tab outside a fenced block (rule 8)");

    return diag.errors() == before;
}

} // namespace umm
