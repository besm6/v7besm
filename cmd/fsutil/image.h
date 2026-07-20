//
// The flat filesystem image: a file of blocks, each BSIZEW words, each word six
// big-endian bytes.
//
// THIS IS THE ONLY PLACE IN THE TOOL THAT KNOWS A WORD HAS BYTES.  Everything
// above it moves whole `Block's -- std::array<Word, 512> -- and never sees the
// serialization.  That is not a style preference, it is the endianness policy:
// the RetroBSD original this is ported from declared `unsigned buf[BSDFS_BSIZE/4]'
// and handed it straight to fs_write_block() in block.c:65 and in all four
// map_block* functions, which quietly made its free list and indirect blocks
// correct only on a little-endian host with a 32-bit `unsigned'.  With the block
// type being an array of Word and write_block() taking it by const reference,
// there is no way to express that mistake here.
//
// The container is FLAT -- block n starts at byte n * BSIZE, and nothing else is
// in the file.  It is not the container SIMH attaches; that one interleaves eight
// service words per zone and stores 8-byte little-endian words with tag bits.
// simh.h converts between the two, and it is the only other file that reads or
// writes bytes.
//
#ifndef B6FSUTIL_IMAGE_H
#define B6FSUTIL_IMAGE_H

#include <cstdio>
#include <string>

#include "fsutil.h"

class Image {
public:
    Image() = default;
    ~Image();

    //
    // Open an existing image.  The block count comes from the file size, which
    // must be a whole number of blocks.
    //
    void open(const std::string &filename, bool for_write);

    //
    // Create a new image of nblk blocks, zero-filled.  An existing file is
    // truncated.  The zero fill is written out in full rather than left as a
    // sparse hole: the file is 6 Mb, and a mkfs whose output depends on the host
    // filesystem's sparse-file behaviour is not worth the seconds it saves.
    //
    void create(const std::string &path, int64_t nblk);

    void close();

    void read_block(int64_t bno, Block &out);
    void write_block(int64_t bno, const Block &in);

    int64_t nblocks() const { return nblk; }
    bool writable() const { return wr; }
    const std::string &name() const { return path; }

    //
    // Word-level serialization, exposed because simh.cpp needs the same six-byte
    // encoding and must not grow a second copy of it.  Both delegate to libaout's
    // fputw()/fgetw(), so this tool and the assembler cannot disagree about what a
    // word looks like on disk.
    //
    static Word get_word(std::FILE *f);
    static void put_word(std::FILE *f, Word w);

    // The image owns a FILE *; copying one would double-close it.
    Image(const Image &)            = delete;
    Image &operator=(const Image &) = delete;

private:
    std::FILE *fp = nullptr;
    std::string path;
    int64_t nblk = 0;
    bool wr      = false;

    void seek_block(int64_t bno);
};

#endif // B6FSUTIL_IMAGE_H
