//
// The superblock.  See superblock.h for why this is a mirror rather than the
// on-disk struct.
//
#include "superblock.h"

#include <ctime>
#include <ostream>

void SuperBlock::load(Image &img)
{
    Block b;
    img.read_block(SUPERB, b);

    magic  = from_word(b[SB_MAGIC]);
    bsize  = from_word(b[SB_BSIZE]);
    inopb  = from_word(b[SB_INOPB]);
    naddr  = from_word(b[SB_NADDR]);
    isize  = from_word(b[SB_ISIZE]);
    fsize  = from_word(b[SB_FSIZE]);
    time   = from_word(b[SB_TIME]);
    tfree  = from_word(b[SB_TFREE]);
    tinode = from_word(b[SB_TINODE]);
    flock  = from_word(b[SB_FLOCK]);
    ilock  = from_word(b[SB_ILOCK]);
    fmod   = from_word(b[SB_FMOD]);
    ronly  = from_word(b[SB_RONLY]);

    nfree = from_word(b[SB_NFREE]);
    for (int i = 0; i < NICFREE; i++)
        free[i] = from_word(b[SB_FREE + i]);

    ninode = from_word(b[SB_NINODE]);
    for (int i = 0; i < NICINOD; i++)
        inode[i] = from_word(b[SB_INODE + i]);

    dirty = false;
}

void SuperBlock::save(Image &img)
{
    //
    // Zero-initialised, so s_fill[17] goes out as zero without being written
    // explicitly -- and so does anything a future field leaves behind.  The
    // kernel's update() writes a whole block here too; matching it means an image
    // this tool wrote and one a running kernel wrote are comparable byte for byte.
    //
    Block b{};

    b[SB_MAGIC]  = to_word(magic);
    b[SB_BSIZE]  = to_word(bsize);
    b[SB_INOPB]  = to_word(inopb);
    b[SB_NADDR]  = to_word(naddr);
    b[SB_ISIZE]  = to_word(isize);
    b[SB_FSIZE]  = to_word(fsize);
    b[SB_TIME]   = to_word(time);
    b[SB_TFREE]  = to_word(tfree);
    b[SB_TINODE] = to_word(tinode);
    b[SB_FLOCK]  = to_word(flock);
    b[SB_ILOCK]  = to_word(ilock);
    b[SB_FMOD]   = to_word(fmod);
    b[SB_RONLY]  = to_word(ronly);

    b[SB_NFREE] = to_word(nfree);
    for (int i = 0; i < NICFREE; i++)
        b[SB_FREE + i] = to_word(free[i]);

    b[SB_NINODE] = to_word(ninode);
    for (int i = 0; i < NICINOD; i++)
        b[SB_INODE + i] = to_word(inode[i]);

    img.write_block(SUPERB, b);
    dirty = false;
}

//
// sbcheck(), kernel/alloc.c:156-180.  Transcribed clause for clause, in the same
// order, with the kernel's own wording -- when this tool accepts an image the
// kernel rejects, the two messages should be the thing that makes the difference
// obvious.
//
bool SuperBlock::validate(std::ostream &err) const
{
    if (magic != int64_t(FS_MAGIC)) {
        err << "not a filesystem\n";
        return false;
    }

    //
    // The geometry words are not ceremony: these constants are in flux in this
    // port -- INOPB went 8 -> 32 and NADDR 13 -> 8 one commit apart -- and an
    // image built by a mkfs one generation out of step would otherwise mount
    // perfectly well and read every inode from the wrong offset.
    //
    if (bsize != BSIZEW || inopb != INOPB || naddr != NADDR) {
        err << "filesystem geometry mismatch\n";
        return false;
    }

    //
    // s_isize is the FIRST DATA BLOCK, not a count of i-list blocks: ialloc()
    // scans `for (adr = SUPERB+1; adr < s_isize; adr++)' and badblock() rejects
    // `bn < s_isize'.  So a garbage value here is a runaway read in the kernel,
    // not merely a wrong answer.
    //
    if (isize <= SUPERB || isize >= fsize) {
        err << "bad filesystem size\n";
        return false;
    }

    if (nfree < 0 || nfree > NICFREE || ninode < 0 || ninode > NICINOD) {
        err << "bad free count\n";
        return false;
    }

    return true;
}

void SuperBlock::print(std::ostream &out) const
{
    const std::time_t t = std::time_t(time);
    char when[64]       = "";
    if (const std::tm *tm = std::localtime(&t))
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm);

    out << "Magic:            " << std::oct << "0" << magic << std::dec << "\n";
    out << "Block size:       " << bsize << " words (" << bsize * NBPW << " bytes)\n";
    out << "Inodes per block: " << inopb << "\n";
    out << "Addrs per inode:  " << naddr << "\n";
    out << "Volume size:      " << fsize << " blocks\n";
    out << "First data block: " << isize << "\n";
    out << "I-list:           blocks " << SUPERB + 1 << ".." << isize - 1 << " ("
        << (isize - SUPERB - 1) * INOPB << " inodes)\n";
    out << "Last update:      " << when << "\n";
    out << "Free blocks:      " << tfree << "\n";
    out << "Free inodes:      " << tinode << "\n";
    out << "Cached free:      " << nfree << " blocks, " << ninode << " inodes\n";
    if (ronly)
        out << "Mounted read-only\n";
    if (fmod)
        out << "Superblock modified\n";
}
