//
// The inode: the i-list, the block map, and byte-granular file I/O.
//
#include "inode.h"

#include <algorithm>
#include <cstring>

namespace {

//
// Byte access inside a block.  Six chars pack into a word, byte 0 in bits 48-41,
// so byte `n' of a block lives in word n/6 at shift 40 - 8*(n%6).
//
// This is the only byte<->word bridge above image.cpp, and it is here rather than
// in fsutil.h because only file data needs it: every other field in the format is
// a whole word.
//
uint8_t block_get_byte(const Block &b, int64_t n)
{
    return uint8_t(b[size_t(n / NBPW)] >> (40 - 8 * (n % NBPW)));
}

void block_put_byte(Block &b, int64_t n, uint8_t v)
{
    const int shift = int(40 - 8 * (n % NBPW));
    Word &w         = b[size_t(n / NBPW)];
    w               = (w & ~(Word(0xFF) << shift)) | (Word(v) << shift);
}

} // namespace

void Inode::get(Filesystem &f, int64_t ino)
{
    fs     = &f;
    number = ino;

    if (ino < 1)
        throw FsError("inode " + std::to_string(ino) + " does not exist");

    const int64_t bno = itod(ino);
    if (bno >= fs->sb.isize)
        throw FsError("inode " + std::to_string(ino) + " is past the end of the i-list");

    Block b;
    fs->image.read_block(bno, b);

    const size_t base = size_t(itoo(ino)) * DI_WORDS;
    mode              = from_word(b[base + DI_MODE]);
    nlink             = from_word(b[base + DI_NLINK]);
    uid               = from_word(b[base + DI_UID]);
    gid               = from_word(b[base + DI_GID]);
    size              = from_word(b[base + DI_SIZE]);
    atime             = from_word(b[base + DI_ATIME]);
    mtime             = from_word(b[base + DI_MTIME]);
    ctime             = from_word(b[base + DI_CTIME]);
    for (int i = 0; i < NADDR; i++)
        addr[i] = from_word(b[base + DI_ADDR + i]);

    dirty = false;
}

void Inode::save(bool force)
{
    if (!dirty && !force)
        return;
    if (!fs)
        throw FsError("saving an inode with no filesystem");

    const int64_t bno = itod(number);
    if (bno >= fs->sb.isize)
        throw FsError("inode " + std::to_string(number) + " is past the end of the i-list");

    //
    // Read-modify-write: INOPB inodes share this block and the other 31 must
    // survive.
    //
    Block b;
    fs->image.read_block(bno, b);

    const size_t base  = size_t(itoo(number)) * DI_WORDS;
    b[base + DI_MODE]  = to_word(mode);
    b[base + DI_NLINK] = to_word(nlink);
    b[base + DI_UID]   = to_word(uid);
    b[base + DI_GID]   = to_word(gid);
    b[base + DI_SIZE]  = to_word(size);
    b[base + DI_ATIME] = to_word(atime);
    b[base + DI_MTIME] = to_word(mtime);
    b[base + DI_CTIME] = to_word(ctime);
    for (int i = 0; i < NADDR; i++)
        b[base + DI_ADDR + i] = to_word(addr[i]);

    fs->image.write_block(bno, b);
    dirty = false;
}

void Inode::clear()
{
    mode  = 0;
    nlink = 0;
    uid   = 0;
    gid   = 0;
    size  = 0;
    atime = 0;
    mtime = 0;
    ctime = 0;
    addr.fill(0);
    dirty = true;
}

//
// bmap(), kernel/subr.c:24-124.
//
// The structure follows the kernel's exactly, including its two loops: the first
// works out HOW MANY levels of indirection this block number needs, leaving the
// answer in `j' and the accumulated shift in `sh'; the second walks that many
// indirect blocks.  Written any other way it is very easy to get the boundary
// between the single and the double indirect off by one -- lbn 517 is the last
// through the single, 518 the first through the double.
//
int64_t Inode::bmap(int64_t lbn, bool allocate)
{
    if (lbn < 0)
        throw FsError("negative block number in bmap");

    //
    // Blocks 0 .. NADDR-NLEVEL-1 are direct.  With NADDR 8 and NLEVEL 2 that is
    // lbn 0..5, straight out of addr[].
    //
    if (lbn < NADDR - NLEVEL) {
        int64_t nb = addr[size_t(lbn)];
        if (nb == 0) {
            if (!allocate)
                return -1; // a hole
            nb                = fs->block_alloc();
            addr[size_t(lbn)] = nb;
            dirty             = true;
        }
        return nb;
    }

    //
    // How many levels?  Subtract the direct blocks, then peel off one indirect
    // block's worth of coverage per level until the number fits.
    //
    int sh     = 0;
    int64_t nb = 1;
    int j;
    lbn -= NADDR - NLEVEL;
    for (j = NLEVEL; j > 0; j--) {
        sh += NSHIFT;
        nb <<= NSHIFT;
        if (lbn < nb)
            break;
        lbn -= nb;
    }
    if (j == 0)
        throw FsError("file too large: block number is past the double indirect");

    //
    // The address in the inode itself: addr[NADDR-2] for the single indirect,
    // addr[NADDR-1] for the double.
    //
    const size_t slot = size_t(NADDR - j);
    nb                = addr[slot];
    if (nb == 0) {
        if (!allocate)
            return -1;
        nb         = fs->block_alloc();
        addr[slot] = nb;
        dirty      = true;
    }

    //
    // Walk down through the indirect blocks: one iteration for the single, two
    // for the double.
    //
    for (; j <= NLEVEL; j++) {
        Block ind;
        fs->image.read_block(nb, ind);

        sh -= NSHIFT;
        const size_t i = size_t((lbn >> sh) & NMASK);
        int64_t next   = from_word(ind[i]);

        if (next == 0) {
            if (!allocate)
                return -1;
            next   = fs->block_alloc();
            ind[i] = to_word(next);
            fs->image.write_block(nb, ind);
        }
        nb = next;
    }

    return nb;
}

int64_t Inode::read(int64_t off, uint8_t *buf, int64_t nbytes)
{
    if (off < 0)
        throw FsError("negative offset in read");
    if (off >= size)
        return 0;

    nbytes = std::min(nbytes, size - off);

    int64_t done = 0;
    while (done < nbytes) {
        //
        // DIVISION, not a shift: a block is 3072 bytes and 3072 is not a power of
        // two.  See the header comment.
        //
        const int64_t lbn = (off + done) / BSIZE;
        const int64_t on  = (off + done) % BSIZE;
        const int64_t n   = std::min(nbytes - done, int64_t(BSIZE) - on);

        const int64_t bno = bmap(lbn, false);
        if (bno < 0) {
            // A hole reads as zeroes, exactly as it does through the kernel.
            std::memset(buf + done, 0, size_t(n));
        } else {
            Block b;
            fs->image.read_block(bno, b);
            for (int64_t i = 0; i < n; i++)
                buf[done + i] = block_get_byte(b, on + i);
        }
        done += n;
    }
    return done;
}

void Inode::write(int64_t off, const uint8_t *buf, int64_t nbytes)
{
    if (off < 0)
        throw FsError("negative offset in write");

    int64_t done = 0;
    while (done < nbytes) {
        const int64_t lbn = (off + done) / BSIZE;
        const int64_t on  = (off + done) % BSIZE;
        const int64_t n   = std::min(nbytes - done, int64_t(BSIZE) - on);

        const int64_t bno = bmap(lbn, true);

        //
        // A partial block is read first so the rest of it survives; a whole one is
        // not, because every byte is about to be replaced and block_alloc()
        // already cleared it.
        //
        Block b{};
        if (n != BSIZE)
            fs->image.read_block(bno, b);

        for (int64_t i = 0; i < n; i++)
            block_put_byte(b, on + i, buf[done + i]);

        fs->image.write_block(bno, b);
        done += n;
    }

    if (off + nbytes > size) {
        size  = off + nbytes;
        dirty = true;
    }
}

//
// Release an indirect block and everything under it.  `level' is 1 for a block
// holding data addresses, 2 for one holding single-indirect addresses.  There is
// no level 3.
//
void Inode::free_indirect(int64_t bno, int level)
{
    if (bno == 0)
        return;

    Block ind;
    fs->image.read_block(bno, ind);

    for (int i = 0; i < NINDIR; i++) {
        const int64_t nb = from_word(ind[size_t(i)]);
        if (nb == 0)
            continue;
        if (level > 1)
            free_indirect(nb, level - 1);
        else
            fs->block_free(nb);
    }
    fs->block_free(bno);
}

void Inode::truncate()
{
    //
    // Back to front, so the free list comes out in an order the next allocation
    // can use.  NADDR-1 is the double indirect, NADDR-2 the single, and everything
    // below them is direct -- there is no third level to unwind.
    //
    free_indirect(addr[NADDR - 1], NLEVEL);
    addr[NADDR - 1] = 0;

    free_indirect(addr[NADDR - 2], NLEVEL - 1);
    addr[NADDR - 2] = 0;

    for (int i = NADDR - NLEVEL - 1; i >= 0; i--) {
        if (addr[size_t(i)] != 0) {
            fs->block_free(addr[size_t(i)]);
            addr[size_t(i)] = 0;
        }
    }

    size  = 0;
    dirty = true;
}
