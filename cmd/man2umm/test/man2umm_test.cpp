//
// Unit tests for cmd/man2umm.
//
// Grouped the way doc/Manual_Page_Format.md is: the title, the headings, the fonts,
// the alternators, the cross-reference rule, the escapes, the escaper, the block
// constructs, the macros, and the lint.  The last group is the important one --
//
//      RoundTrip.*  asserts read_umm(write_umm(read_man(x))) == read_man(x)
//
// which is the guarantee that a renderer written against read_umm() can display
// everything the converter writes.  Everything else here is a special case of it
// with the expected text pinned down.
//
#include <gtest/gtest.h>

#include <sstream>

#include "man2umm.h"

using namespace umm;

namespace {

// Convert a roff page and return the Markdown.
std::string conv(const std::string &roff, const std::string &path = "t/foo.1")
{
    std::istringstream in(roff);
    Diag d;
    Doc doc = read_man(in, path, d);
    std::ostringstream out;
    write_umm(out, doc);
    return out.str();
}

// The same, but keeping the diagnostics.
std::string conv_diag(const std::string &roff, Diag &d, const std::string &path = "t/foo.1")
{
    std::istringstream in(roff);
    Doc doc = read_man(in, path, d);
    std::ostringstream out;
    write_umm(out, doc);
    return out.str();
}

// A minimal well-formed page, so a test can say only what it is about.
std::string page(const std::string &body)
{
    return ".TH FOO 1\n.SH NAME\nfoo \\- a thing\n.SH DESCRIPTION\n" + body;
}

// The DESCRIPTION section's text, which is what most tests are asserting on.
std::string desc(const std::string &md)
{
    size_t i = md.find("## DESCRIPTION\n");
    if (i == std::string::npos)
        return "<no DESCRIPTION>";
    std::string t = md.substr(i + 15);
    while (!t.empty() && t.front() == '\n')
        t.erase(0, 1);
    while (!t.empty() && t.back() == '\n')
        t.pop_back();
    return t;
}

std::string body_of(const std::string &roff)
{
    return desc(conv(page(roff)));
}

bool same(const std::vector<Span> &a, const std::vector<Span> &b);
bool same(const std::vector<Block> &a, const std::vector<Block> &b);

bool same(const std::vector<Span> &a, const std::vector<Span> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].font != b[i].font || a[i].text != b[i].text)
            return false;
    return true;
}

bool same(const std::vector<Block> &a, const std::vector<Block> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].kind != b[i].kind || a[i].info != b[i].info || a[i].text != b[i].text)
            return false;
        if (!same(a[i].tag, b[i].tag) || !same(a[i].body, b[i].body) || !same(a[i].child, b[i].child))
            return false;
    }
    return true;
}

// Convert, write, read back, and compare the two models.
::testing::AssertionResult round_trips(const std::string &roff)
{
    std::istringstream in(roff);
    Diag d1;
    Doc a = read_man(in, "t/foo.1", d1);

    std::ostringstream md;
    write_umm(md, a);

    std::istringstream back(md.str());
    Diag d2;
    Doc b = read_umm(back, "t/foo.1.umm", d2);

    if (a.name != b.name || a.section != b.section || a.extra != b.extra)
        return ::testing::AssertionFailure() << "title differs\n" << md.str();
    if (!same(a.blocks, b.blocks)) {
        std::ostringstream da, db;
        dump(da, a);
        dump(db, b);
        return ::testing::AssertionFailure()
               << "model differs\n--- markdown ---\n"
               << md.str() << "--- from roff ---\n"
               << da.str() << "--- read back ---\n"
               << db.str();
    }
    return ::testing::AssertionSuccess();
}

// ---------------------------------------------------------------- the title ----

TEST(Title, PlainSection)
{
    EXPECT_NE(conv(".TH LS 1\n").find("# LS(1)\n"), std::string::npos);
}

TEST(Title, LetteredSection)
{
    EXPECT_NE(conv(".TH FSCK 1M\n").find("# FSCK(1M)\n"), std::string::npos);
    EXPECT_NE(conv(".TH PRINTF 3S\n").find("# PRINTF(3S)\n"), std::string::npos);
}

TEST(Title, QuotedName)
{
    EXPECT_NE(conv(".TH \"CHDIR\" 2\n").find("# CHDIR(2)\n"), std::string::npos);
}

TEST(Title, FourthFieldBecomesABracket)
{
    EXPECT_NE(conv(".TH GETPW 3 deprecated\n").find("# GETPW(3) [deprecated]\n"), std::string::npos);
    EXPECT_NE(conv(".TH CURSES 3X \"April 23, 1986\"\n", "lib/libcurses/curses.3")
                  .find("# CURSES(3) [April 23, 1986]\n"),
              std::string::npos);
}

TEST(Title, MissingIsAnError)
{
    std::istringstream in(".SH NAME\nfoo \\- a thing\n");
    Diag d;
    read_man(in, "t/foo.1", d);
    EXPECT_GT(d.errors(), 0);
}

// -------------------------------------------------------------- the headings ----

TEST(Heading, SectionAndSubsection)
{
    std::string md = conv(".TH FOO 1\n.SH \"SEE ALSO\"\n.SS Where the bit lives\n");
    EXPECT_NE(md.find("## SEE ALSO\n"), std::string::npos);
    EXPECT_NE(md.find("### Where the bit lives\n"), std::string::npos);
}

// ----------------------------------------------------------------- the fonts ----

TEST(Font, WholeLineMacros)
{
    EXPECT_EQ(body_of(".B cat\n"), "**cat**");
    EXPECT_EQ(body_of(".I file\n"), "*file*");
}

TEST(Font, QuotedArgumentIsOneRun)
{
    EXPECT_EQ(body_of(".B \"tee: too many output files\"\n"), "**tee: too many output files**");
}

TEST(Font, SeveralArgumentsJoinWithSpaces)
{
    EXPECT_EQ(body_of(".B a b c\n"), "**a b c**");
}

TEST(Font, SmallIsRoman)
{
    // nroff rendered .SM at the same size on a terminal, so it carries nothing.
    EXPECT_EQ(body_of(".SM BESM\n"), "BESM");
}

TEST(Font, BareMacroTakesTheNextLine)
{
    EXPECT_EQ(body_of(".B\ncat\n"), "**cat**");
}

TEST(Font, InlineFontEscapes)
{
    EXPECT_EQ(body_of("a \\fBb\\fR c\n"), "a **b** c");
    EXPECT_EQ(body_of("a \\fIb\\fP c\n"), "a *b* c");
}

// ----------------------------------------------------------- the alternators ----

TEST(Alternator, JoinsWithNoSpace)
{
    // roff's own rule, and the reason `.RB ( \-t )' reads (-t) rather than ( -t ).
    EXPECT_EQ(body_of(".RB ( \\-t )\n"), "(**-t**)");
    EXPECT_EQ(body_of(".IR malloc ;\n"), "*malloc*;");
    EXPECT_EQ(body_of(".BR $IFS )\n"), "**$IFS**)");
}

TEST(Alternator, BoldItalicNeedsTheJoiner)
{
    // Without the \& the closing ** would be followed by a letter, and rule 2 would
    // not read it as a delimiter at all.
    EXPECT_EQ(body_of(".BI b size\n"), "**b**\\&*size*");
}

TEST(Alternator, RomanItalicBrackets)
{
    EXPECT_EQ(body_of(".RI [ who ]\n"), "[*who*]");
}

// ------------------------------------------------------- v7 `word' quoting ----

TEST(Quoting, BecomesALiteralRun)
{
    // v7 writes a backquote, the word and an apostrophe to mean "quoted as itself",
    // which is what a literal run means.  330 of the corpus's 536 escaped backquotes
    // were this construct being transliterated instead of recognized.
    EXPECT_EQ(body_of("the \\`environment' variable\n"), "the `environment` variable");
    EXPECT_EQ(body_of("Beware of \\`cat a b >a' here\n"), "Beware of `cat a b >a` here");
}

TEST(Quoting, OneCharacterCounts)
{
    EXPECT_EQ(body_of("the argument \\`\\-' is\n"), "the argument `-` is");
}

TEST(Quoting, TakesTheFontWithIt)
{
    // .RB ` x ' -- the bold is v7 emphasising the quoted character, and a literal
    // run says the same thing with one construct instead of three.
    EXPECT_EQ(body_of(".RB ` ? '\n"), "`?`");
}

TEST(Quoting, AnApostropheInsideAWordIsNotAClose)
{
    // ...or `foo' bar's would quote through to the possessive.
    EXPECT_EQ(body_of("a \\`foo' and bar's thing\n"), "a `foo` and bar's thing");
}

TEST(Quoting, SpaceBeforeTheApostropheIsNotAQuotation)
{
    EXPECT_EQ(body_of("a \\`foo ' b\n"), "a \\`foo ' b");
}

TEST(Quoting, TwoQuotationsOnOneLineStayApart)
{
    EXPECT_EQ(body_of("\\`a' and \\`b'\n"), "`a` and `b`");
}

TEST(Quoting, EmptyQuotationIsLeftAlone)
{
    EXPECT_EQ(body_of("a \\`' b\n"), "a \\`' b");
}

TEST(Quoting, DoubledIsOneRunToo)
{
    // v7 doubles the quotes for an outer level; the closing run must match.
    EXPECT_EQ(body_of("an \\`\\`='' here\n"), "an `=` here");
    // A doubled open with a single close is malformed roff.  All that is promised
    // is that it degrades locally rather than swallowing the rest of the line.
    EXPECT_NE(body_of("an \\`\\`=' here\n").find("here"), std::string::npos);
}

TEST(Quoting, InAHeading)
{
    // brk.2 titles a subsection with a quotation, and a heading gets the same two
    // passes as anything else.
    std::string md = conv(".TH FOO 1\n.SS \\`end' is declared as an array\n");
    EXPECT_NE(md.find("### `end` is declared as an array\n"), std::string::npos);
}

TEST(RoundTrip, Quoting)
{
    EXPECT_TRUE(round_trips(page("the \\`environment' and \\`name=value' forms\n")));
}

// ------------------------------------------------------- the cross-reference ----

TEST(Xref, CollapsesTheRoffIdiom)
{
    EXPECT_EQ(body_of(".IR open (2),\n"), "open(2),");
    EXPECT_EQ(body_of(".BR chmod (2)\n"), "chmod(2)");
}

TEST(Xref, RecognizedInlineToo)
{
    EXPECT_EQ(body_of("See cp(1) and mv(1).\n"), "See cp(1) and mv(1).");
}

TEST(Xref, LetteredSection)
{
    EXPECT_EQ(body_of(".IR fopen (3s)\n"), "fopen(3s)");
}

TEST(Xref, NotAPrototype)
{
    // `read(int' is a declaration, not a reference: the paren is not followed by a
    // section digit and a close.
    EXPECT_EQ(body_of("int read(int fildes);\n"), "int read(int fildes);");
}

TEST(Xref, DoesNotStartMidWord)
{
    EXPECT_EQ(body_of("sprintf(3) is one word\n"), "sprintf(3) is one word");
}

TEST(Alternator, EscapedSpaceStaysInsideItsArgument)
{
    // roff splits on UNESCAPED whitespace, so this is three arguments and not five.
    EXPECT_EQ(body_of(".IR execv \\ and \\ execl\n"), "*execv* and *execl*");
}

TEST(Font, BareMacroLastsOneLineOnly)
{
    // stat.2 writes a bare .I above a list of type names and expects the italic to
    // stop at the end of that line.
    EXPECT_EQ(body_of(".I\nino_t, off_t, time_t,\nname various widths\n"),
              "*ino_t, off_t, time_t,* name various widths");
}

// --------------------------------------------------------------- the escapes ----

TEST(Escape, HyphenAndSpace)
{
    EXPECT_EQ(body_of("a \\- b\n"), "a - b");
    EXPECT_EQ(body_of("a\\ b\n"), "a b");
}

TEST(Escape, ThinSpacesVanishAndTheNeighboursJoin)
{
    EXPECT_EQ(body_of("a\\|b\n"), "ab");
    EXPECT_EQ(body_of("a\\^b\n"), "ab");
}

TEST(Escape, Backslash)
{
    EXPECT_EQ(body_of("a \\e b\n"), "a \\\\ b");
}

TEST(Escape, TypeSizeIsDropped)
{
    EXPECT_EQ(body_of("\\s-2small\\s0 again\n"), "small again");
}

TEST(Escape, SpecialCharacters)
{
    EXPECT_EQ(body_of("a \\(em b\n"), "a \xE2\x80\x94 b");
    EXPECT_EQ(body_of("\\(*p r\\(mu r\n"), "\xCF\x80 r\xC3\x97 r");
    EXPECT_EQ(body_of("\\(+- \\(sc \\(fm\n"), "\xC2\xB1 \xC2\xA7 \xE2\x80\xB2");
}

TEST(Escape, TwoSpecialsStayAscii)
{
    // \(mi is arithmetic prose and \(or is a pipe: both are ASCII in the thing
    // being described, so a typographic character there would be a change.
    EXPECT_EQ(body_of("returns \\(mi1\n"), "returns -1");
    EXPECT_EQ(body_of("a \\(or b\n"), "a | b");
}

TEST(Escape, CommentToEndOfLine)
{
    EXPECT_EQ(body_of("visible \\\" hidden\n"), "visible");
}

TEST(Escape, NestedWidthFunction)
{
    // intro.2 lays its errno table out with a motion whose width is itself a width
    // function.  Taking the first delimiter as the close leaves the inner argument
    // behind as text.
    EXPECT_EQ(body_of("a\\h'\\w'EIO'u'b\n"), "ab");
}

TEST(Escape, WidthFunctionWarns)
{
    Diag d;
    conv_diag(page("a\\w'wide'ub\n"), d);
    EXPECT_GT(d.warnings(), 0);
}

TEST(Escape, SuperscriptWarns)
{
    Diag d;
    conv_diag(page("10\\u9\\d\n"), d);
    EXPECT_GT(d.warnings(), 0);
}

// --------------------------------------------------------------- the escaper ----

TEST(Escaper, PowerOfTwoNeedsNoEscape)
{
    // Rule 2: the ** has a digit on both sides, so it is not a delimiter.
    EXPECT_EQ(body_of("2**41-1 is not 2**48-1\n"), "2**41-1 is not 2**48-1");
}

TEST(Escaper, UnderscoreIsNeverEscaped)
{
    EXPECT_EQ(body_of("time_t and d_ino and SIG_DFL\n"), "time_t and d_ino and SIG_DFL");
}

TEST(Escaper, BracketsAreNotEscaped)
{
    EXPECT_EQ(body_of("ac_comm[10] and [who]\n"), "ac_comm[10] and [who]");
}

TEST(Escaper, LoneAsteriskIsEscaped)
{
    EXPECT_EQ(body_of("a * b\n"), "a \\* b");
}

TEST(Escaper, ColumnOneIsProtected)
{
    // A wrapped line that happens to begin with `- ' would start a bullet list.
    EXPECT_EQ(body_of("- not a list\n"), "\\- not a list");
}

// -------------------------------------------------------- the block constructs ----

TEST(Block, TaggedParagraphIsADefinition)
{
    EXPECT_EQ(body_of(".TP\n.B \\-l\nLong format.\n"), "**-l**\n: Long format.");
}

TEST(Block, IpWithATagIsADefinition)
{
    EXPECT_EQ(body_of(".IP FLAGS\nThe flags.\n"), "**FLAGS**\n: The flags.");
}

TEST(Block, BareIpIsAnIndentedDisplay)
{
    EXPECT_EQ(body_of(".IP\ncat file\n"), "> cat file");
}

TEST(Block, BulletList)
{
    EXPECT_EQ(body_of(".IP \\(bu 3\nfirst\n.IP \\(bu 3\nsecond\n"), "- first\n\n- second");
}

TEST(Block, OrderedList)
{
    EXPECT_EQ(body_of(".IP 1.\nfirst\n"), "1. first");
}

TEST(Block, NoFillWithoutFontsIsAFence)
{
    EXPECT_EQ(body_of(".nf\n  a   b\n  c   d\n.fi\n"), "```\n  a   b\n  c   d\n```");
}

TEST(Block, NoFillWithFontsIsALineBlock)
{
    EXPECT_EQ(body_of(".nf\n.B one\n.B two\n.fi\n"), "| **one**\n| **two**");
}

TEST(Block, CDeclarationGetsTheInfoString)
{
    EXPECT_EQ(body_of(".nf\nstruct direct {\n};\n.fi\n"), "```c\nstruct direct {\n};\n```");
}

TEST(Block, BreakMakesALineBlock)
{
    EXPECT_EQ(body_of("one\n.br\ntwo\n"), "| one\n| two");
}

TEST(Block, RsReIsAQuote)
{
    EXPECT_EQ(body_of(".RS\nindented\n.RE\n"), "> indented");
}

TEST(Block, UnbalancedReIsAnError)
{
    Diag d;
    conv_diag(page("text\n.RE\n"), d);
    EXPECT_GT(d.errors(), 0);
}

TEST(Block, ParagraphIsRefilled)
{
    EXPECT_EQ(body_of("one\ntwo\nthree\n"), "one two three");
}

// ------------------------------------------------- the macros, expanded away ----

TEST(Macro, StringDefinition)
{
    EXPECT_EQ(body_of(".ds w \\fIwin\\fP\nthe \\*w argument\n"), "the *win* argument");
}

TEST(Macro, TwoLetterStringName)
{
    EXPECT_EQ(body_of(".ds OK [\\|\nan \\*(OK bracket\n"), "an [ bracket");
}

TEST(Macro, UndefinedStringRendersAsNothingAndWarns)
{
    Diag d;
    std::string md = conv_diag(page("an \\*S undefined\n"), d);
    EXPECT_EQ(desc(md), "an undefined");
    EXPECT_GT(d.warnings(), 0);
}

TEST(Macro, UserDefinedMacroIsExpanded)
{
    // lib/libc/man/intro.2's `.de en', which is the corpus's only user macro.
    std::string md = body_of(".de en\n.HP\n\\\\$1  \\\\$2  \\\\$3\n.br\n..\n"
                             ".en 1 EPERM \"Not owner\n"
                             "Typically this indicates an attempt to modify a file.\n");
    EXPECT_NE(md.find("EPERM"), std::string::npos);
    EXPECT_NE(md.find("Not owner"), std::string::npos);
    EXPECT_NE(md.find("Typically"), std::string::npos);
}

TEST(Macro, DroppedRequestsAreSilent)
{
    // The typography a filled renderer has no use for: no output, no diagnostic.
    Diag d;
    std::string md = conv_diag(page(".ns\n.PD 0\n.DT\ntext\n"), d);
    EXPECT_EQ(desc(md), "text");
    EXPECT_EQ(d.warnings(), 0);
    EXPECT_EQ(d.errors(), 0);
}

TEST(Macro, UnknownRequestWarns)
{
    Diag d;
    conv_diag(page(".XYZZY nothing\ntext\n"), d);
    EXPECT_GT(d.warnings(), 0);
}

TEST(Conditional, NroffBranchIsTakenAndTroffBranchIsNot)
{
    EXPECT_EQ(body_of(".if n terminal\n"), "terminal");
    EXPECT_EQ(body_of(".if t typeset\nplain\n"), "plain");
}

// ----------------------------------------------------------------- .so ----------

TEST(SourceRequest, UnresolvableLeavesAMarkerAndFails)
{
    Diag d;
    std::string md = conv_diag(page(".so /usr/include/sys/nosuch.h\n"), d);
    EXPECT_GT(d.errors(), 0);
    EXPECT_NE(md.find("man2umm: FIXME"), std::string::npos);
}

// ------------------------------------------------------------------ UTF-8 ------

TEST(Utf8, PassesThroughByteForByte)
{
    // README section 11: this machine is byte-transparent, so a Cyrillic string is
    // written as itself and no -Kutf8 note is needed.
    EXPECT_EQ(body_of("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\n"),
              "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
}

TEST(Utf8, FontStreamCountsCharactersNotBytes)
{
    std::istringstream in(page(".B \xD0\xBF\xD1\x80\n"));
    Diag d;
    Doc doc = read_man(in, "t/foo.1", d);
    std::ostringstream f;
    font_stream(f, doc);
    // Four bytes of Cyrillic, all bold; the counts must agree with groff's, which
    // counts characters.  This is the one place the oracle could silently drift.
    EXPECT_NE(f.str().find("BBBB"), std::string::npos);
}

// ----------------------------------------------------------- the round trip ----

TEST(RoundTrip, Minimal)
{
    EXPECT_TRUE(round_trips(page("Just a paragraph.\n")));
}

TEST(RoundTrip, EveryInlineFont)
{
    EXPECT_TRUE(round_trips(page("roman \\fBbold\\fR \\fIitalic\\fR and open(2).\n")));
}

TEST(RoundTrip, Alternators)
{
    EXPECT_TRUE(round_trips(page(".BI b size\n.RB ( \\-t )\n.IR malloc ;\n")));
}

TEST(RoundTrip, DefinitionList)
{
    EXPECT_TRUE(round_trips(page(".TP\n.B \\-l\nLong format, and a good deal more text so that "
                                 "the definition body has to wrap over more than one line.\n"
                                 ".TP\n.B \\-C\nColumns.\n")));
}

TEST(RoundTrip, Lists)
{
    EXPECT_TRUE(round_trips(page(".IP \\(bu 3\nfirst\n.IP \\(bu 3\nsecond\n")));
    EXPECT_TRUE(round_trips(page(".IP 1.\nfirst\n.IP 2.\nsecond\n")));
}

TEST(RoundTrip, FencedAndLineBlocks)
{
    EXPECT_TRUE(round_trips(page(".nf\n  a   b\n.fi\n")));
    EXPECT_TRUE(round_trips(page(".nf\n.B one\n.B two\n.fi\n")));
}

TEST(RoundTrip, Quote)
{
    EXPECT_TRUE(round_trips(page(".RS\nindented text\n.RE\n")));
}

TEST(RoundTrip, LiteralAndEscapes)
{
    EXPECT_TRUE(round_trips(page("2**41-1 and time_t and a \\- dash\n")));
}

TEST(RoundTrip, LongParagraphWraps)
{
    std::string long_para;
    for (int i = 0; i < 40; i++)
        long_para += "word ";
    EXPECT_TRUE(round_trips(page(long_para + "\n")));
}

TEST(RoundTrip, Comments)
{
    EXPECT_TRUE(round_trips(".\\\" UNIX V7 source code: see /COPYRIGHT for details.\n" +
                            page("text\n")));
}

// ------------------------------------------------------------------ the lint ----

// Read a dialect page and lint it; returns the number of errors.
int lint_of(const std::string &md, const std::string &path = "t/foo.1.umm")
{
    std::istringstream in(md);
    Diag d;
    Doc doc = read_umm(in, path, d);
    lint(doc, path, d);
    return d.errors();
}

const char *const GOOD = "# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\nIt does it.\n";

TEST(Lint, AcceptsACanonicalPage)
{
    EXPECT_EQ(lint_of(GOOD), 0);
}

TEST(Lint, Rule1NoTitle)
{
    EXPECT_GT(lint_of("## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\nx\n"), 0);
}

TEST(Lint, Rule2TitleMustMatchTheFilename)
{
    EXPECT_GT(lint_of(GOOD, "t/bar.1.umm"), 0);
    EXPECT_GT(lint_of("# FOO(8)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\nx\n"), 0);
}

TEST(Lint, Rule3NameMustComeFirst)
{
    EXPECT_GT(lint_of("# FOO(1)\n\n## DESCRIPTION\n\nx\n\n## NAME\n\nfoo - a thing\n"), 0);
}

TEST(Lint, Rule3NameLineMustBeInShape)
{
    // ` - ' with one ASCII hyphen and a space either side: an index parses on it.
    EXPECT_GT(lint_of("# FOO(1)\n\n## NAME\n\nfoo -- a thing\n\n## DESCRIPTION\n\nx\n"), 0);
}

TEST(Lint, Rule3PageNeedNotListItself)
{
    // A page named for a TOPIC lists the topic's members: exec(2) lists execl,
    // execv, execle and never `exec'.  A warning, not an error.
    std::istringstream in("# FOO(1)\n\n## NAME\n\nbar, baz - things\n\n## DESCRIPTION\n\nx\n");
    Diag d;
    Doc doc = read_umm(in, "t/foo.1.umm", d);
    EXPECT_TRUE(lint(doc, "t/foo.1.umm", d));
    EXPECT_GT(d.warnings(), 0);
}

TEST(Lint, Rule3NameMayListSeveral)
{
    EXPECT_EQ(lint_of("# FOO(1)\n\n## NAME\n\nfoo, bar, baz - things\n\n## DESCRIPTION\n\nx\n"), 0);
}

TEST(Lint, Rule4DescriptionIsRequired)
{
    EXPECT_GT(lint_of("# FOO(1)\n\n## NAME\n\nfoo - a thing\n"), 0);
}

TEST(Lint, Rule6FixmeMarkerIsFatal)
{
    EXPECT_GT(lint_of("# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\n"
                      "<!-- man2umm: FIXME .so /usr/include/sys/dir.h -->\n"),
              0);
}

TEST(Lint, Rule8TabOutsideAFence)
{
    EXPECT_GT(lint_of("# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\na\tb\n"), 0);
}

TEST(Lint, Rule8TabInsideAFenceIsFine)
{
    EXPECT_EQ(lint_of("# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\n```\na\tb\n```\n"),
              0);
}

TEST(Lint, Rule9MarkedUpCrossReference)
{
    EXPECT_GT(lint_of("# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\nSee **open(2)**.\n"),
              0);
}

TEST(Lint, UnconventionalHeadingIsOnlyAWarning)
{
    std::istringstream in("# FOO(1)\n\n## NAME\n\nfoo - a thing\n\n## DESCRIPTION\n\nx\n\n"
                          "## HOW MANY FILES\n\nNineteen.\n");
    Diag d;
    Doc doc = read_umm(in, "t/foo.1.umm", d);
    EXPECT_TRUE(lint(doc, "t/foo.1.umm", d));
    EXPECT_GT(d.warnings(), 0);
}

// ------------------------------------------------------------------ the CLI ----

TEST(Cli, NoArgumentsIsAUsageError)
{
    char a0[] = "b6man2umm";
    char *av[] = { a0, nullptr };
    EXPECT_EQ(cli(1, av), 2);
}

TEST(Cli, UnknownFlag)
{
    char a0[] = "b6man2umm";
    char a1[] = "-Q";
    char *av[] = { a0, a1, nullptr };
    EXPECT_EQ(cli(2, av), 2);
}

TEST(Cli, MissingFileIsAnError)
{
    char a0[] = "b6man2umm";
    char a1[] = "/nonexistent/page.1";
    char *av[] = { a0, a1, nullptr };
    EXPECT_EQ(cli(2, av), 1);
}

} // namespace
