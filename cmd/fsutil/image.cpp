//
// The flat filesystem image.  See image.h for why this is the only file in the
// tool that deals in bytes.
//
#include "image.h"

#include <cerrno>
#include <cstring>

//
// libaout is compiled as C and cross/besm6/b.out.h has no extern "C" guard, so
// the include is wrapped here -- the same idiom cmd/libaout/test/word_test.cpp
// uses for the same reason.
//
extern "C" {
#include "besm6/b.out.h"
}

Image::~Image()
{
    close();
}

//
// One word: six bytes, big-endian, high half-word first.  Delegated rather than
// open-coded so that a change to the word format reaches this tool and b6as
// together.  See cmd/libaout/README.md for the encoding.
//
Word Image::get_word(std::FILE *f)
{
    return fgetw(f) & WORD_MASK;
}

void Image::put_word(std::FILE *f, Word w)
{
    fputw(w & WORD_MASK, f);
}

void Image::open(const std::string &filename, bool for_write)
{
    close();

    fp = std::fopen(filename.c_str(), for_write ? "r+b" : "rb");
    if (!fp)
        throw FsError(filename + ": " + std::strerror(errno));

    if (std::fseek(fp, 0, SEEK_END) != 0)
        throw FsError(filename + ": not seekable");
    const long size = std::ftell(fp);
    if (size < 0)
        throw FsError(filename + ": cannot determine size");

    //
    // A partial trailing block means the file is truncated or is not an image at
    // all.  Refusing here turns a mystifying short read deep in the free-list walk
    // into one line at open time.
    //
    if (size == 0 || size % BSIZE != 0)
        throw FsError(filename + ": size " + std::to_string(size) + " is not a whole number of " +
                      std::to_string(BSIZE) + "-byte blocks");

    path = filename;
    nblk = size / BSIZE;
    wr   = for_write;
}

void Image::create(const std::string &filename, int64_t count)
{
    close();

    if (count < 3)
        throw FsError("a filesystem needs at least a boot block, a superblock and an i-list");

    fp = std::fopen(filename.c_str(), "w+b");
    if (!fp)
        throw FsError(filename + ": " + std::strerror(errno));

    path = filename;
    nblk = count;
    wr   = true;

    const Block zero{};
    for (int64_t b = 0; b < count; b++)
        write_block(b, zero);
}

void Image::close()
{
    if (fp) {
        std::fclose(fp);
        fp = nullptr;
    }
    path.clear();
    nblk = 0;
    wr   = false;
}

void Image::seek_block(int64_t bno)
{
    if (bno < 0 || bno >= nblk)
        throw FsError("block " + std::to_string(bno) + " is outside the image (" +
                      std::to_string(nblk) + " blocks)");

    if (std::fseek(fp, long(bno * BSIZE), SEEK_SET) != 0)
        throw FsError(path + ": seek to block " + std::to_string(bno) + " failed");
}

void Image::read_block(int64_t bno, Block &out)
{
    if (!fp)
        throw FsError("read from a closed image");

    seek_block(bno);
    for (int i = 0; i < BSIZEW; i++)
        out[i] = get_word(fp);

    if (std::ferror(fp) || std::feof(fp))
        throw FsError(path + ": short read at block " + std::to_string(bno));
}

void Image::write_block(int64_t bno, const Block &in)
{
    if (!fp)
        throw FsError("write to a closed image");
    if (!wr)
        throw FsError(path + ": opened read-only");

    seek_block(bno);
    for (int i = 0; i < BSIZEW; i++)
        put_word(fp, in[i]);

    if (std::ferror(fp))
        throw FsError(path + ": write failed at block " + std::to_string(bno));
}
