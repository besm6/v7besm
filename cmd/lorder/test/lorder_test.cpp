//
// Unit tests for the cmd/lorder shell script.
//
// lorder turns a set of object files into the dependency edge-list that tsort
// consumes to pick a link order.  It is a POSIX shell script that shells out to
// nm, so there is no engine to drive in-process: instead each test assembles
// small objects with known cross-references -- using the real b6as on real
// assembly source -- runs lorder over them with NM pointing at the real b6nm,
// and checks the edges it prints.  This exercises the whole toolchain end to
// end: b6as -> b6nm -> lorder.
//
#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Assemble `source` into "<stem>.o" with the real b6as, writing the source to
// "<stem>.s" first.  Returns the object path.  A defined ".globl" label lists as
// "T name"; an undefined reference lists as "U name".  -u is deliberately NOT
// passed, so undefined names stay external references instead of being errors.
std::string assemble(const std::string &stem, const std::string &source)
{
    std::string src = stem + ".s";
    std::string obj = stem + ".o";
    {
        std::ofstream f(src, std::ios::trunc);
        f << source;
    }
    std::string cmd = "\"" B6AS_EXE "\" -o \"" + obj + "\" \"" + src + "\"";
    EXPECT_EQ(std::system(cmd.c_str()), 0) << cmd;
    return obj;
}

// Run lorder over the given object files (with NM = the real b6nm) and return
// everything it wrote to stdout.
std::string run_lorder(const std::vector<std::string> &objs)
{
    std::string cmd = "NM=\"" B6NM_EXE "\" /bin/sh \"" LORDER_SH "\"";
    for (const auto &o : objs)
        cmd += " \"" + o + "\"";

    FILE *p = popen(cmd.c_str(), "r");
    EXPECT_NE(p, nullptr) << "popen " << cmd;
    if (!p)
        return {};

    std::string out;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
        out.append(buf, n);
    pclose(p);
    return out;
}

// True if `line` appears as a complete line in `out`.
bool has_line(const std::string &out, const std::string &line)
{
    std::string needle = line + "\n";
    if (out.compare(0, needle.size(), needle) == 0)
        return true; // first line
    return out.find("\n" + needle) != std::string::npos;
}

// funcA is a defined global; funcB is referenced here but defined elsewhere.
const char *const SRC_A = R"(
        .globl funcA
funcA:  atx 0
        uj funcB        ; external reference, defined in b
)";

// funcB is a defined global.
const char *const SRC_B = R"(
        .globl funcB
funcB:  atx 0
)";

} // namespace

// a.o references funcB which b.o defines: lorder must emit the edge "a b" plus a
// self-edge for each object (so a node with no deps still survives tsort), and
// must NOT emit the reverse edge.
TEST(Lorder, DependencyEdge)
{
    std::string a = assemble("lorder_a", SRC_A);
    std::string b = assemble("lorder_b", SRC_B);

    std::string out = run_lorder({ a, b });

    EXPECT_TRUE(has_line(out, a + " " + b)) << out; // a depends on b
    EXPECT_TRUE(has_line(out, a + " " + a)) << out; // self-edges keep every node
    EXPECT_TRUE(has_line(out, b + " " + b)) << out;
    EXPECT_FALSE(has_line(out, b + " " + a)) << out; // no reverse dependency

    unlink("lorder_a.s");
    unlink("lorder_b.s");
    unlink(a.c_str());
    unlink(b.c_str());
}

// A single object passed once still yields its self-edge: lorder duplicates a
// lone *.o argument so nm prints the per-file header its pipeline needs.
TEST(Lorder, SingleObjectSelfEdge)
{
    std::string a = assemble("lorder_solo", SRC_B);

    std::string out = run_lorder({ a });

    EXPECT_TRUE(has_line(out, a + " " + a)) << out;

    unlink("lorder_solo.s");
    unlink(a.c_str());
}
