//
// Unit tests for the archive serializers getint()/putint(),
// getarhdr()/putarhdr() and fputran().
//
// The fd-based ar/ranlib helpers must encode the same on-disk layout that
// their FILE*-based siblings (fgetint/fgetarhdr) decode, so an archive written
// by ar reads back correctly in ld/nm. Each test writes with the fd-based
// routine and reads back with BOTH the fd-based reader AND the FILE*-based
// reader, asserting they agree.
//
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

// The library is compiled as C and the besm6 headers have no extern "C" guard.
extern "C" {
#include "besm6/b.out.h"
#include "besm6/ar.h"
#include "besm6/ranlib.h"
}

// Open a fresh temp file, returning its fd; the path is filled in for reopen.
static int make_tmp(char *path)
{
    std::snprintf(path, L_tmpnam + 16, "/tmp/aout_ar_XXXXXX");
    int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    return fd;
}

// putint then read back with both getint (fd) and fgetint (FILE*).
TEST(ArInt, RoundTrip) {
    char path[L_tmpnam + 16];
    int fd = make_tmp(path);
    ASSERT_GE(fd, 0);

    const int value = 0x123456;  // low half-word only (24 bits round-trip)
    ASSERT_EQ(putint(fd, value), 1);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    uword_t via_fd = 0;
    ASSERT_EQ(getint(fd, &via_fd), 1);
    EXPECT_EQ(via_fd, static_cast<uword_t>(value));
    close(fd);

    FILE *f = std::fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    int via_file = 0;
    ASSERT_EQ(fgetint(f, &via_file), 1);
    EXPECT_EQ(via_file, value);
    std::fclose(f);
    unlink(path);
}

// putint zeroes the high (first) half-word and encodes the value in the low
// (second) half-word; the 24-bit boundary values round-trip through both readers.
TEST(ArInt, Boundaries) {
    const int values[] = { 0, 0xFFFFFF };
    for (int value : values) {
        char path[L_tmpnam + 16];
        int fd = make_tmp(path);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(putint(fd, value), 1);

        // Raw bytes: high (first) half-word is zero, low half-word holds value.
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
        unsigned char raw[6];
        ASSERT_EQ(read(fd, raw, 6), 6);
        EXPECT_EQ(raw[0], 0) << "value 0x" << std::hex << value;
        EXPECT_EQ(raw[1], 0) << "value 0x" << std::hex << value;
        EXPECT_EQ(raw[2], 0) << "value 0x" << std::hex << value;

        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
        uword_t via_fd = ~0ULL;
        ASSERT_EQ(getint(fd, &via_fd), 1);
        EXPECT_EQ(via_fd, static_cast<uword_t>(value));
        close(fd);

        FILE *f = std::fopen(path, "rb");
        ASSERT_NE(f, nullptr);
        int via_file = -1;
        ASSERT_EQ(fgetint(f, &via_file), 1);
        EXPECT_EQ(via_file, value);
        std::fclose(f);
        unlink(path);
    }
}

// ar_date / ar_size are full 48-bit big-endian words. Use values that exercise
// all 48 bits -- including bits 24-31, which the old two-half-word split dropped.
TEST(ArHdr, DateSizeFullWord) {
    char path[L_tmpnam + 16];
    int fd = make_tmp(path);
    ASSERT_GE(fd, 0);

    struct ar_hdr out{};
    char name[] = "member.o";
    out.ar_name = name;
    out.ar_date = 0x123456789ABCL;
    out.ar_size = 0x0FEDCBA98765L;
    ASSERT_EQ(putarhdr(fd, &out), 1);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    struct ar_hdr via_fd{};
    ASSERT_EQ(getarhdr(fd, &via_fd), 1);
    EXPECT_EQ(via_fd.ar_date, out.ar_date);
    EXPECT_EQ(via_fd.ar_size, out.ar_size);
    std::free(via_fd.ar_name);
    close(fd);

    FILE *f = std::fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    struct ar_hdr via_file{};
    ASSERT_EQ(fgetarhdr(f, &via_file), 1);
    EXPECT_EQ(via_file.ar_date, out.ar_date);
    EXPECT_EQ(via_file.ar_size, out.ar_size);
    std::free(via_file.ar_name);
    std::fclose(f);
    unlink(path);
}

// The member name is length-prefixed on disk (1 length byte + name, padded to
// a word), so the header is variable-sized and NUL-terminated in memory. Write
// a header with putarhdr and read it back with both getarhdr (fd) and fgetarhdr
// (FILE*), for name lengths spanning the boundaries: 1 byte, a mid-length name,
// and the 255-byte maximum.
static void RoundTripName(const std::string &name) {
    char path[L_tmpnam + 16];
    int fd = make_tmp(path);
    ASSERT_GE(fd, 0);

    std::string mutable_name = name;  // ar_name is a non-const char *
    struct ar_hdr out{};
    out.ar_name = mutable_name.data();
    out.ar_date = 0x0ABCDE;
    out.ar_uid  = 0x111;
    out.ar_gid  = 0x222;
    out.ar_mode = 0644;
    out.ar_size = 0x0FEDCB;
    ASSERT_EQ(putarhdr(fd, &out), 1);

    // The on-disk header size must be a whole number of words and match arhdrsz().
    off_t written = lseek(fd, 0, SEEK_CUR);
    EXPECT_EQ(written, arhdrsz(&out)) << "name len " << name.size();
    EXPECT_EQ(written % 6, 0) << "header word-aligned, name len " << name.size();

    auto expect_eq = [&](const struct ar_hdr &in) {
        ASSERT_NE(in.ar_name, nullptr);
        EXPECT_STREQ(in.ar_name, name.c_str());
        EXPECT_EQ(in.ar_date, out.ar_date);
        EXPECT_EQ(in.ar_uid,  out.ar_uid);
        EXPECT_EQ(in.ar_gid,  out.ar_gid);
        EXPECT_EQ(in.ar_mode, out.ar_mode);
        EXPECT_EQ(in.ar_size, out.ar_size);
    };

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    struct ar_hdr via_fd{};
    ASSERT_EQ(getarhdr(fd, &via_fd), 1);
    expect_eq(via_fd);
    std::free(via_fd.ar_name);
    close(fd);

    FILE *f = std::fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    struct ar_hdr via_file{};
    ASSERT_EQ(fgetarhdr(f, &via_file), 1);
    expect_eq(via_file);
    std::free(via_file.ar_name);
    std::fclose(f);
    unlink(path);
}

TEST(ArHdr, RoundTripShortName) { RoundTripName("a"); }
TEST(ArHdr, RoundTripMidName)   { RoundTripName("util.o"); }
TEST(ArHdr, RoundTripLongName)  { RoundTripName(std::string(ARMAXNAME, 'x')); }

// fputran then read back with fgetran.
TEST(Ranlib, RoundTrip) {
    char name[] = "_symbol";
    struct ranlib out{};
    out.ran_len  = static_cast<word_t>(sizeof(name) - 1);
    out.ran_off  = 0x0ABCDE;  // low half-word only
    out.ran_name = name;

    FILE *f = std::tmpfile();
    ASSERT_NE(f, nullptr);
    fputran(&out, f);
    std::rewind(f);

    struct ranlib in{};
    ASSERT_EQ(fgetran(f, &in), 1);
    EXPECT_EQ(in.ran_len, out.ran_len);
    EXPECT_EQ(in.ran_off, out.ran_off);
    ASSERT_NE(in.ran_name, nullptr);
    EXPECT_STREQ(in.ran_name, name);
    std::free(in.ran_name);
    std::fclose(f);
}

// A zero-length ranlib entry marks end of the index: fgetran returns 0 and
// allocates nothing.
TEST(Ranlib, EmptyEntryAtEof) {
    FILE *f = std::tmpfile();
    ASSERT_NE(f, nullptr);
    putc(0, f);  // single zero length byte
    std::rewind(f);

    struct ranlib in{};
    in.ran_name = nullptr;
    EXPECT_EQ(fgetran(f, &in), 0);
    EXPECT_EQ(in.ran_name, nullptr);  // no allocation for an empty entry
    std::fclose(f);
}
