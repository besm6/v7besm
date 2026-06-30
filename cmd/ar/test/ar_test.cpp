//
// Unit tests for the cmd/ar archiver engine.
//
// The engine is driven through ar_run(argc, argv), which parses its own argv
// exactly like the b6ar command but returns the exit code instead of calling
// exit(), so each operation runs in-process.  Results are inspected by walking
// the produced archive with the very same libaout decoders that ld uses
// (getint/getarhdr), which is what makes the AR-10 word-alignment guarantee
// meaningful: ld steps to the next member with `ar_size + ARHDRSZ`.
//
#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// The engine and the shared on-disk decoders are compiled as C.
extern "C" {
#include "archive.h"

#include "besm6/ar.h"
#include "besm6/b.out.h"
}

#define W 6 // длина слова БЭСМ-6 в байтах

namespace {

// Name of the currently running test, e.g. "Ar.CreateRoundTrip".
std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

void write_file(const std::string &path, const std::vector<unsigned char> &bytes)
{
    int fd = creat(path.c_str(), 0644);
    ASSERT_GE(fd, 0) << "creat " << path;
    if (!bytes.empty())
        ASSERT_EQ(write(fd, bytes.data(), bytes.size()), (ssize_t)bytes.size());
    close(fd);
}

std::vector<unsigned char> read_file(const std::string &path)
{
    int fd = open(path.c_str(), O_RDONLY);
    EXPECT_GE(fd, 0) << "open " << path;
    std::vector<unsigned char> out;
    unsigned char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        out.insert(out.end(), buf, buf + n);
    close(fd);
    return out;
}

// A few distinct payload bytes so padding zeros are easy to tell apart.
std::vector<unsigned char> payload(unsigned char seed, int len)
{
    std::vector<unsigned char> v(len);
    for (int i = 0; i < len; i++)
        v[i] = (unsigned char)(seed + i);
    return v;
}

// Run b6ar with the given arguments; returns the exit code.  ar_run() rewrites
// entries of argv, so a fresh vector is built for every call.
int run_ar(std::vector<std::string> args)
{
    // s is non-const: ar_run() rewrites argv in place (trim() strips slashes),
    // and the non-const string::data() overload is what yields a writable char*.
    std::vector<char *> argv(args.size());
    // cppcheck-suppress constParameterReference
    std::transform(args.begin(), args.end(), argv.begin(), [](std::string &s) { return s.data(); });
    argv.push_back(nullptr);
    return ar_run((int)args.size(), argv.data());
}

// Compare a payload against the (word-padded) member data: the original bytes
// at the front, zeros for the rest.
void expect_member_payload(const std::vector<unsigned char> &data,
                           const std::vector<unsigned char> &orig)
{
    ASSERT_GE(data.size(), orig.size());
    EXPECT_EQ(std::vector<unsigned char>(data.begin(), data.begin() + orig.size()), orig);
    for (size_t i = orig.size(); i < data.size(); i++)
        EXPECT_EQ(data[i], 0) << "pad byte " << i;
}

struct Member {
    std::string name;
    long size; // size recorded in the header (already word-padded)
    std::vector<unsigned char> data;
    long offset; // byte offset of this member's header in the file
};

// Walk an archive with the shared decoders, recording each member.
std::vector<Member> read_archive(const std::string &path)
{
    std::vector<Member> v;
    int fd = open(path.c_str(), O_RDONLY);
    EXPECT_GE(fd, 0) << "open " << path;
    if (fd < 0)
        return v;

    uword_t magic = 0;
    EXPECT_EQ(getint(fd, &magic), 1);
    EXPECT_EQ(magic, (uword_t)ARMAG);

    long off = W; // past the magic word
    struct ar_hdr h;
    while (getarhdr(fd, &h)) {
        Member m;
        char nm[sizeof(h.ar_name) + 1];
        memcpy(nm, h.ar_name, sizeof(h.ar_name));
        nm[sizeof(h.ar_name)] = '\0';
        m.name   = nm;
        m.size   = (long)h.ar_size;
        m.offset = off;
        m.data.resize(m.size);
        if (m.size > 0)
            EXPECT_EQ(read(fd, m.data.data(), m.size), (ssize_t)m.size);
        v.push_back(m);
        off += ARHDRSZ + m.size;
    }
    close(fd);
    return v;
}

} // namespace

// Create an archive from two members whose lengths are NOT multiples of the
// word size, then check the originals survive intact.
TEST(Ar, CreateRoundTrip)
{
    std::string base = current_test_name();
    std::string a    = base + "_a";
    std::string b    = base + "_b";
    std::string lib  = base + ".a";

    auto pa = payload(0x10, 7);
    auto pb = payload(0x40, 13);
    write_file(a, pa);
    write_file(b, pb);

    ASSERT_EQ(run_ar({ "b6ar", "rc", lib, a, b }), 0);

    auto m = read_archive(lib);
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m[0].name, a);
    EXPECT_EQ(m[1].name, b);
    // Original bytes are preserved at the front of each (padded) member.
    expect_member_payload(m[0].data, pa);
    expect_member_payload(m[1].data, pb);

    unlink(a.c_str());
    unlink(b.c_str());
    unlink(lib.c_str());
}

// AR-10: every stored member size is a multiple of W, the data is zero-padded
// up to that size, and consequently each member header lands on a word
// boundary -- the exact invariant ld relies on when stepping members.
TEST(Ar, MembersWordAligned)
{
    std::string base = current_test_name();
    std::string a    = base + "_a"; // 7 bytes  -> padded to 12
    std::string b    = base + "_b"; // 12 bytes -> already aligned, pad 0
    std::string c    = base + "_c"; // 1 byte   -> padded to 6
    std::string lib  = base + ".a";

    auto pa = payload(0x10, 7);
    auto pb = payload(0x40, 12);
    auto pc = payload(0x70, 1);
    write_file(a, pa);
    write_file(b, pb);
    write_file(c, pc);

    ASSERT_EQ(run_ar({ "b6ar", "rc", lib, a, b, c }), 0);

    auto m = read_archive(lib);
    ASSERT_EQ(m.size(), 3u);

    const std::vector<unsigned char> *orig[] = { &pa, &pb, &pc };
    for (size_t k = 0; k < m.size(); k++) {
        EXPECT_EQ(m[k].size % W, 0) << "member " << k << " size not word-aligned";
        EXPECT_EQ(m[k].offset % W, 0) << "member " << k << " offset not word-aligned";
        EXPECT_EQ((size_t)m[k].size, (orig[k]->size() + W - 1) / W * W);
        // Bytes past the original payload must be zero padding.
        for (long i = (long)orig[k]->size(); i < m[k].size; i++)
            EXPECT_EQ(m[k].data[i], 0) << "member " << k << " pad byte " << i;
    }

    // The whole archive is a whole number of words.
    EXPECT_EQ((long)read_file(lib).size() % W, 0);

    unlink(a.c_str());
    unlink(b.c_str());
    unlink(c.c_str());
    unlink(lib.c_str());
}

// Extract members back out and confirm the original payload is recovered
// (the extracted file carries the stored, word-padded length).
TEST(Ar, Extract)
{
    std::string base = current_test_name();
    std::string a    = base + "_a";
    std::string lib  = base + ".a";

    auto pa = payload(0x21, 7);
    write_file(a, pa);
    ASSERT_EQ(run_ar({ "b6ar", "rc", lib, a }), 0);
    unlink(a.c_str()); // remove the source so extraction must recreate it

    ASSERT_EQ(run_ar({ "b6ar", "x", lib, a }), 0);

    auto got = read_file(a);
    ASSERT_EQ((long)got.size() % W, 0);
    expect_member_payload(got, pa);

    unlink(a.c_str());
    unlink(lib.c_str());
}

// Deleting a member leaves the rest of the archive intact.
TEST(Ar, Delete)
{
    std::string base = current_test_name();
    std::string a    = base + "_a";
    std::string b    = base + "_b";
    std::string lib  = base + ".a";

    auto pa = payload(0x10, 7);
    auto pb = payload(0x40, 13);
    write_file(a, pa);
    write_file(b, pb);
    ASSERT_EQ(run_ar({ "b6ar", "rc", lib, a, b }), 0);

    ASSERT_EQ(run_ar({ "b6ar", "d", lib, a }), 0);

    auto m = read_archive(lib);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].name, b);
    expect_member_payload(m[0].data, pb);

    unlink(a.c_str());
    unlink(b.c_str());
    unlink(lib.c_str());
}
