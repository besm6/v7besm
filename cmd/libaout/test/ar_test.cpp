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
    int via_fd = 0;
    ASSERT_EQ(getint(fd, &via_fd), 1);
    EXPECT_EQ(via_fd, value);
    close(fd);

    FILE *f = std::fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    int via_file = 0;
    ASSERT_EQ(fgetint(f, &via_file), 1);
    EXPECT_EQ(via_file, value);
    std::fclose(f);
    unlink(path);
}

// putarhdr then read back with both getarhdr (fd) and fgetarhdr (FILE*).
TEST(ArHdr, RoundTrip) {
    char path[L_tmpnam + 16];
    int fd = make_tmp(path);
    ASSERT_GE(fd, 0);

    // Each multi-byte field is encoded as 24-bit half-words, so keep values
    // within the bits that survive the layout.
    struct ar_hdr out{};
    for (int i = 0; i < 14; i++)
        out.ar_name[i] = static_cast<char>('a' + i);
    out.ar_date = 0x0ABCDE;
    out.ar_uid  = 0x111;
    out.ar_gid  = 0x222;
    out.ar_mode = 0644;
    out.ar_size = 0x0FEDCB;
    ASSERT_EQ(putarhdr(fd, &out), 1);

    auto expect_eq = [&](const struct ar_hdr &in) {
        for (int i = 0; i < 14; i++)
            EXPECT_EQ(in.ar_name[i], out.ar_name[i]) << "name byte " << i;
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
    close(fd);

    FILE *f = std::fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    struct ar_hdr via_file{};
    ASSERT_EQ(fgetarhdr(f, &via_file), 1);
    expect_eq(via_file);
    std::fclose(f);
    unlink(path);
}

// fputran then read back with fgetran.
TEST(Ranlib, RoundTrip) {
    char name[] = "_symbol";
    struct ranlib out{};
    out.ran_len  = static_cast<short>(sizeof(name) - 1);
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
