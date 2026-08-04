//
// Does cmd/sim/etcfiles.cpp still hold what etc/ holds?
//
// The six files are transcribed by hand into raw string literals, so the copy can drift from
// the tree and nothing in the build would notice: a b6sim serving a stale /etc/passwd looks
// exactly like a b6sim serving the right one, and the first symptom would be a test failing
// under the booted kernel and passing here -- days later, since that half is `weekly'.  This
// file is the guard, and cmd/sim/params.cpp is the same file for the same reason: one
// translation unit whose whole job is to prove that b6sim's copy of something still agrees
// with the original.  params.cpp can static_assert; a raw string against a text file cannot
// be checked before it runs, so this one is a test.
//
// HOW IT REACHES THE TREE.  B6_TREE_ROOT, a -D from test/CMakeLists.txt -- not a -I, because
// cmd/sim/CMakeLists.txt keeps include/ off this library's search path on purpose, and not a
// relative path either: gtest_discover_tests runs from the build directory, so "../../../etc"
// would encode the assumption that the build tree sits exactly one level under the source
// tree.  The top-level Makefile happens to satisfy that and nothing guarantees it.
//
// NOT EVERY FILE IS CHECKED THE SAME WAY, because not every file is served the same way
// (etcfiles.cpp says why).  Three rules, one per kind:
//
//   passwd, group, ttys   BYTE FOR BYTE.  These are the files a program PARSES -- getpwent,
//                         getgrent, ttyslot -- and whose content is diffed across both
//                         worlds by cmd/quot, cmd/fsck and lib/test/pwent.  A difference
//                         here is a wrong answer, not a shorter file.
//   termcap               IDENTICAL ONCE COMMENTS AND BLANK LINES ARE DROPPED.  b6sim's
//                         copy carries the five terminal entries and not the 34-line
//                         header; tgetent(3) never sees the difference, and every
//                         capability it can see must still be the tree's.
//   motd, rc              ABRIDGED, so the claim is only that nothing was INVENTED: every
//                         line b6sim serves is a line of the file it came from.
//
// AND THE LAST TEST IS THE ONE THAT MATTERS MOST.  The three above catch an edit to a file
// b6sim already serves; walking etc/ catches a SEVENTH file joining B6_ETC that nobody
// taught b6sim about, which is this artifact happening again.
//
#include <dirent.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "etcfiles.h"

// The whole of a host file, or "" if it cannot be opened.
static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

// The guest path with its leading '/' stripped IS the tree path: etc/ is staged to /etc
// unchanged (etc/CMakeLists.txt), which is the whole reason one string can name both.
static std::string tree_path(const char *guest)
{
    return std::string(B6_TREE_ROOT) + guest;
}

// The lines of a text, in order.  A trailing newline does not make an extra empty line.
static std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);)
        out.push_back(l);
    return out;
}

// ... and the same with blank lines and comments dropped, which is all termcap's header is.
static std::vector<std::string> code_lines_of(const std::string &text)
{
    std::vector<std::string> all = lines_of(text);
    std::vector<std::string> out;
    std::copy_if(all.begin(), all.end(), std::back_inserter(out),
                 [](const std::string &l) { return !l.empty() && l[0] != '#'; });
    return out;
}

static std::string served_text(const char *path)
{
    const EtcFiles::File *f = EtcFiles::find(path);
    return (f == nullptr) ? std::string() : std::string(f->text, f->size);
}

//
// The parsed three, byte for byte.  A difference of exactly one byte is almost always the
// raw-string trap etcfiles.cpp's header names: a newline after R"ETC( is content.
//
TEST(EtcFiles, ParsedFilesMatchTheTree)
{
    for (const char *path : { "/etc/passwd", "/etc/group", "/etc/ttys" }) {
        std::string tree = slurp(tree_path(path));
        std::string ours = served_text(path);
        ASSERT_FALSE(tree.empty()) << "cannot read " << tree_path(path);
        ASSERT_FALSE(ours.empty()) << path << " is not served";

        ASSERT_EQ(ours.size(), tree.size()) << path << ": sizes differ";
        for (size_t k = 0; k < ours.size(); k++)
            ASSERT_EQ(ours[k], tree[k]) << path << ": first difference at byte " << k;
    }
}

//
// termcap, once the header comment and the blank lines are gone.  Every capability line
// tgetent(3) can reach must be the tree's, character for character -- an entry abridged in
// the middle would make a curses program behave one way here and another on the image.
//
TEST(EtcFiles, TermcapEntriesMatchTheTree)
{
    std::vector<std::string> tree = code_lines_of(slurp(tree_path("/etc/termcap")));
    std::vector<std::string> ours = code_lines_of(served_text("/etc/termcap"));

    ASSERT_FALSE(tree.empty());
    ASSERT_EQ(ours.size(), tree.size()) << "b6sim serves a different number of termcap lines";
    for (size_t k = 0; k < ours.size(); k++)
        ASSERT_EQ(ours[k], tree[k]) << "/etc/termcap: line " << k << " differs";
}

//
// motd and rc are abridged on purpose, so the only claim left is that nothing was invented:
// every line served is a line of the file it came from.  A stale abridgement -- text that
// used to be in etc/motd and is not any more -- fails here.
//
TEST(EtcFiles, AbridgedFilesInventNothing)
{
    for (const char *path : { "/etc/motd", "/etc/rc" }) {
        std::vector<std::string> tree = lines_of(slurp(tree_path(path)));
        std::set<std::string> have(tree.begin(), tree.end());
        ASSERT_FALSE(tree.empty()) << "cannot read " << tree_path(path);

        for (const std::string &l : lines_of(served_text(path)))
            EXPECT_EQ(have.count(l), 1u)
                << path << ": b6sim serves a line that is not in " << tree_path(path) << ": " << l;
    }
}

//
// Every file in etc/ is served.  CMakeLists.txt is the one entry that is not a file of the
// root filesystem; everything else there is staged onto the image and must be here too.
//
TEST(EtcFiles, EveryStagedFileIsServed)
{
    std::set<std::string> served;
    for (int i = 0; i < EtcFiles::count(); i++)
        served.insert(EtcFiles::entry(i).path);

    DIR *d = ::opendir((std::string(B6_TREE_ROOT) + "/etc").c_str());
    ASSERT_NE(d, nullptr);
    int seen = 0;
    for (const struct dirent *e = ::readdir(d); e != nullptr; e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == ".." || name == "CMakeLists.txt")
            continue;
        seen++;
        EXPECT_EQ(served.count("/etc/" + name), 1u)
            << "etc/" << name << " is staged onto the image but b6sim does not serve it";
    }
    ::closedir(d);
    EXPECT_EQ(seen, EtcFiles::count());
}

//
// The match is exact, as SimKernel::dev_minor()'s is.  Nothing normalises a path here, and
// the header says why that is enough.
//
TEST(EtcFiles, LookupIsExact)
{
    ASSERT_NE(EtcFiles::find("/etc/passwd"), nullptr);
    EXPECT_EQ(EtcFiles::find("/etc/passwd")->size, 145u);

    EXPECT_EQ(EtcFiles::find("etc/passwd"), nullptr);
    EXPECT_EQ(EtcFiles::find("/etc/passwd/"), nullptr);
    EXPECT_EQ(EtcFiles::find("/etc//passwd"), nullptr);
    EXPECT_EQ(EtcFiles::find("/etc/nosuch"), nullptr);
    EXPECT_EQ(EtcFiles::find(""), nullptr);
}
