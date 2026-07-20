//
// Unix v7 filesystem images for the BESM-6, as seen from the host.
//
// This header owns the ON-DISK FORMAT: the constants, the word type, and the two
// conversions every field crosses on its way to or from a block.  Nothing here
// describes host storage -- the mirror structs in superblock.h and inode.h do that,
// and they are deliberately NOT the on-disk layout.  See image.h for why.
//
// WHY THE CONSTANTS ARE RESPELLED RATHER THAN INCLUDED.  The kernel keeps them in
// include/sys/param.h, which is #define-only by design so that b6as can read it.
// Three things make it unusable from C++ code that also uses the host's libc:
//
//   - it defines NULL as 0 (param.h:93), which redefines the C++ library's own
//     spelling and fails the build under -Werror;
//   - itod(), itoo() and makedev() cast to daddr_t/ino_t/dev_t -- the KERNEL's
//     typedefs, from sys/types.h, which mean one 48-bit word.  On the host those
//     names belong to libc and mean something else entirely;
//   - ROOTINO and SUPERB carry the same casts.
//
// So the values are written out here, and params.cpp -- one translation unit that
// includes param.h in isolation and does nothing else -- static_asserts every one
// of them against the kernel's own.  A kernel that retunes INOPB or DIRSIZ breaks
// the BUILD of this tool rather than the images it writes.  That file is the whole
// point of this arrangement; do not delete it as redundant.
//
#ifndef B6FSUTIL_H
#define B6FSUTIL_H

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

//
// A BESM-6 machine word: 48 bits.  Held in a uint64_t on the host, matching
// libaout's uword_t (cross/besm6/types.h) so the two can be passed back and forth
// without a cast.  The top 16 bits of the host type are always zero in a value
// that came from, or is going to, a disk block.
//
using Word = uint64_t;

constexpr Word WORD_MASK = 07777777777777777ULL; // bits 48-1

//
// Geometry.  See include/sys/param.h for the reasoning behind each; params.cpp
// asserts they still agree with it.
//
constexpr int NBPW   = 6;    // bytes in a word -- the target's sizeof(int)
constexpr int BSIZEW = 512;  // words in a block
constexpr int BSIZE  = 3072; // bytes in a block (BSIZEW * NBPW)
constexpr int NINDIR = 512;  // daddr_t per indirect block
constexpr int NSHIFT = 9;    // LOG2(NINDIR)
constexpr int NMASK  = 0777; // NINDIR-1

constexpr int NADDR    = 8;   // disk addresses in an inode: 6 direct, 1 single, 1 double
constexpr int NLEVEL   = 2;   // levels of indirection -- there is NO triple indirect
constexpr int INOPB    = 32;  // inodes per block
constexpr int INOSHIFT = 5;   // LOG2(INOPB)
constexpr int INOMASK  = 037; // INOPB-1

constexpr int DIRSIZ   = 18;   // chars in a directory name (3 words)
constexpr int DIRWORDS = 4;    // words in a struct direct
constexpr int DIRENTSZ = 24;   // bytes in a struct direct
constexpr int DIRPB    = 128;  // directory entries per block
constexpr int DIRSHIFT = 7;    // LOG2(DIRPB)
constexpr int DIRMASK  = 0177; // DIRPB-1

constexpr int NICINOD = 160; // free i-nodes cached in the superblock
constexpr int NICFREE = 320; // free blocks cached in the superblock

constexpr Word FS_MAGIC = 0123456701234ULL;

//
// param.h spells these two with a cast -- ROOTINO is ((ino_t)2), SUPERB is
// ((daddr_t)1) -- so they are respelled here for the reason in the header comment.
//
constexpr int64_t ROOTINO = 2; // i-number of the root directory
constexpr int64_t SUPERB  = 1; // block number of the superblock
constexpr int64_t BOOTB   = 0; // block number of the boot block

//
// The volume.  One EC-5052 drive is 1000 zones of two blocks each and there are no
// partitions, so a whole-drive filesystem is the only arrangement the kernel offers
// (kernel/dev/md.c).  mdstart() rejects a transfer past MDNBLK, so an image built
// larger than this does not fail at mkfs time -- it fails much later, as an I/O
// error on a zone the backing file does not reach.
//
constexpr int64_t MDNBLK = 2000;

//
// A block, and the only spelling of one.
//
// It is an array of WORDS, never of bytes and never of `unsigned'.  That is the
// whole endianness policy of this tool: Image::write_block() serializes each word
// itself, so no caller can hand raw host storage to the disk.  The RetroBSD
// original this is ported from did exactly that -- `unsigned buf[BSDFS_BSIZE/4]'
// passed straight to fs_write_block() in block.c and in all four map_block*
// functions -- which quietly made its images little-endian-host-only.  Here the
// same mistake does not compile.
//
using Block = std::array<Word, BSIZEW>;

//
// Errors.  Every failure in this tool is a malformed image or an out-of-range
// value, both of which want a message and an exit, not an errno.
//
class FsError : public std::runtime_error {
public:
    explicit FsError(const std::string &msg) : std::runtime_error(msg) {}
};

//
// The value range of a target `int'.
//
// Every on-disk field -- daddr_t, ino_t, time_t, dev_t, off_t -- is one word
// holding a 41-bit SIGNED value: sign in bit 41, bits 48-42 clear.  The host's
// time_t, off_t and st_size are 64-bit, so a host file over a terabyte or a bogus
// timestamp would silently truncate.  Everything crosses through to_word(), which
// reports instead.  See include/sys/types.h.
//
constexpr int64_t WORD_MAX = (int64_t(1) << 40) - 1;
constexpr int64_t WORD_MIN = -(int64_t(1) << 40);
constexpr Word SIGN_BIT    = Word(1) << 40;
constexpr Word VALUE_MSK   = (Word(1) << 41) - 1;

//
// Host value -> disk word.  Throws rather than wrapping: a value that does not fit
// is a bug in the caller or a hostile input file, never something to truncate.
//
inline Word to_word(int64_t v)
{
    if (v < WORD_MIN || v > WORD_MAX)
        throw FsError("value " + std::to_string(v) + " does not fit a BESM-6 int");
    return Word(v) & VALUE_MSK;
}

//
// Disk word -> host value, sign-extending bit 41.
//
inline int64_t from_word(Word w)
{
    w &= VALUE_MSK;
    return (w & SIGN_BIT) ? int64_t(w) - int64_t(SIGN_BIT << 1) : int64_t(w);
}

//
// Six chars to a word, byte 0 in bits 48-41.
//
// The shift distance for byte k is 40 - 8k, which is the same arithmetic the
// machine's own byte pointer uses (doc/Besm6_Data_Representation.md section 7, and
// BytePointer::peek_byte in cmd/sim/memory.h).  Getting this backwards produces a
// filesystem that looks perfect in `b6fsutil -v' and is unreadable by namei(),
// which is why the test pins the raw bytes and not just the round trip.
//
inline Word pack_chars(const char *s, int n)
{
    Word w = 0;
    for (int k = 0; k < n && k < NBPW; k++) {
        if (s[k] == '\0')
            break;
        w |= Word(uint8_t(s[k])) << (40 - 8 * k);
    }
    return w;
}

//
// The inverse.  Stops at the first NUL, as the kernel's own name comparison does.
//
inline void unpack_chars(Word w, char *s, int n)
{
    for (int k = 0; k < n && k < NBPW; k++)
        s[k] = char(uint8_t(w >> (40 - 8 * k)));
}

//
// Inumber to block number, and to the slot within that block.  v7's `2*INOPB - 1'
// bias places inode 1 at block 2 slot 0: the i-list starts just past the boot block
// and the superblock.  Spelled in terms of INOPB, as param.h now is, so that
// resizing the inode cannot leave them behind.
//
inline int64_t itod(int64_t ino)
{
    return (ino + 2 * INOPB - 1) >> INOSHIFT;
}

inline int itoo(int64_t ino)
{
    return int((ino + 2 * INOPB - 1) & INOMASK);
}

//
// Device numbers.  param.h's makedev() casts to the kernel's dev_t; this is the
// same arithmetic without it.
//
inline int64_t makedev(int maj, int min)
{
    return (int64_t(maj) << 8) | (min & 0377);
}

inline int major_of(int64_t dev)
{
    return int(dev >> 8);
}

inline int minor_of(int64_t dev)
{
    return int(dev & 0377);
}

//
// File mode bits, from include/sys/stat.h.  Note the absence of S_IFLNK: this v7
// has no symbolic links, so the manifest has no `symlink' keyword and there is no
// encoding to invent for one.
//
constexpr int64_t IFMT  = 0170000;
constexpr int64_t IFDIR = 0040000;
constexpr int64_t IFCHR = 0020000;
constexpr int64_t IFBLK = 0060000;
constexpr int64_t IFREG = 0100000;
constexpr int64_t IFMPC = 0030000;
constexpr int64_t IFMPB = 0070000;
constexpr int64_t ISUID = 04000;
constexpr int64_t ISGID = 02000;
constexpr int64_t ISVTX = 01000;

//
// Command-line state, threaded by reference rather than kept in globals.
//
// The RetroBSD original used file-scope `verbose', `extract', `add', `check',
// `mount' and `scan'.  Four of those names collide with libc functions on macOS
// and every one of them trips -Wshadow against a parameter somewhere, both of
// which are build failures here.
//
struct Options {
    int verbose = 0;
    bool fix    = false;
    int64_t uid = 0;
    int64_t gid = 0;
};

#endif // B6FSUTIL_H
