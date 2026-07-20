//
// Directories.  See dir.h for why this is written from kernel/nami.c rather than
// ported from the RetroBSD source.
//
#include "dir.h"

#include <cstring>

namespace dir {
namespace {

//
// Where entry `ent' lives: which logical block of the directory, and which slot
// within it.  DIRPB entries tile a block exactly, so both are a shift and a mask
// -- the arithmetic nami.c:141-144 makes a point of recovering.
//
int64_t ent_to_lbn(int64_t ent)
{
    return ent >> DIRSHIFT;
}

size_t ent_to_word(int64_t ent)
{
    return size_t(ent & DIRMASK) * DE_WORDS;
}

//
// The number of entries a directory holds.  i_size is in BYTES and is exactly
// nentries * DIRENTSZ -- see the header comment on why it is not block-rounded.
//
int64_t entry_count(const Inode &dp)
{
    return dp.size / DIRENTSZ;
}

void unpack_entry(const Block &b, size_t base, DirEntry &out)
{
    out.ino = from_word(b[base + DE_INO]);
    std::memset(out.name, 0, sizeof(out.name));
    for (int w = 0; w < DIRSIZ / NBPW; w++)
        unpack_chars(b[base + DE_NAME + w], out.name + w * NBPW, NBPW);
    out.name[DIRSIZ] = '\0';
}

void pack_entry(Block &b, size_t base, const DirEntry &in)
{
    //
    // The name is NUL-padded to the full DIRSIZ, not merely terminated: namei()
    // compares all DIRSIZ characters (nami.c:174-176), so a short name whose tail
    // holds stale bytes would not match itself.
    //
    char padded[DIRSIZ] = {};
    const size_t n      = std::min(std::strlen(in.name), size_t(DIRSIZ));
    std::memcpy(padded, in.name, n);

    b[base + DE_INO] = to_word(in.ino);
    for (int w = 0; w < DIRSIZ / NBPW; w++)
        b[base + DE_NAME + w] = pack_chars(padded + w * NBPW, NBPW);
}

//
// v7's name comparison: DIRSIZ characters, NUL-padded on both sides.  A longer
// name is TRUNCATED to DIRSIZ and then matches -- that is the behaviour, not a
// bug, and dir::name_is_truncated() exists so callers can warn about it.
//
bool name_equals(const char *entry, const std::string &want)
{
    char padded[DIRSIZ] = {};
    const size_t n      = std::min(want.size(), size_t(DIRSIZ));
    std::memcpy(padded, want.c_str(), n);

    return std::memcmp(entry, padded, DIRSIZ) == 0;
}

//
// Walk the directory a BLOCK at a time, calling `fn' for each slot; stop early
// when it returns true.
//
// Reading a block per slot instead of per block is the obvious way to write this
// and it is quadratic: a 1000-entry directory then costs a million 3 Kb reads,
// which measured at 135 seconds for a single dir::enter() loop.  namei() does not
// make that mistake -- nami.c:150 re-reads only `if (on == 0 || bp == NULL)', i.e.
// on a block boundary -- and neither does this.  get() and put() keep the
// random-access form for fsck and the tests, where it is called once.
//
bool scan(Inode &dp, const std::function<bool(int64_t, const DirEntry &)> &fn)
{
    const int64_t n = entry_count(dp);
    DirEntry e;
    Block b{};
    int64_t have_lbn = -1;

    for (int64_t ent = 0; ent < n; ent++) {
        const int64_t lbn = ent_to_lbn(ent);

        if (lbn != have_lbn) {
            const int64_t bno = dp.bmap(lbn, false);
            if (bno < 0)
                b.fill(0); // a hole: every slot in it reads as unused
            else
                dp.fs->image.read_block(bno, b);
            have_lbn = lbn;
        }

        unpack_entry(b, ent_to_word(ent), e);
        if (fn(ent, e))
            return true;
    }
    return false;
}

} // namespace

bool get(Inode &dp, int64_t ent, DirEntry &out)
{
    if (ent < 0 || ent >= entry_count(dp))
        return false;

    const int64_t bno = dp.bmap(ent_to_lbn(ent), false);
    if (bno < 0) {
        // A hole in a directory: every slot in it reads as unused.
        out = DirEntry{};
        return true;
    }

    Block b;
    dp.fs->image.read_block(bno, b);
    unpack_entry(b, ent_to_word(ent), out);
    return true;
}

void put(Inode &dp, int64_t ent, const DirEntry &in)
{
    if (ent < 0)
        throw FsError("negative directory entry number");

    const int64_t bno = dp.bmap(ent_to_lbn(ent), true);

    Block b;
    dp.fs->image.read_block(bno, b);
    pack_entry(b, ent_to_word(ent), in);
    dp.fs->image.write_block(bno, b);

    // Extending the directory grows it by exactly one entry, never to a block.
    const int64_t need = (ent + 1) * DIRENTSZ;
    if (need > dp.size) {
        dp.size  = need;
        dp.dirty = true;
    }
}

void each(Inode &dp, const std::function<void(int64_t, const DirEntry &)> &fn)
{
    scan(dp, [&](int64_t ent, const DirEntry &e) {
        fn(ent, e);
        return false;
    });
}

int64_t lookup(Inode &dp, const std::string &name)
{
    int64_t found = 0;

    scan(dp, [&](int64_t, const DirEntry &e) {
        if (e.ino == 0)
            return false; // an unused slot, as namei() skips it
        if (!name_equals(e.name, name))
            return false;
        found = e.ino;
        return true;
    });

    return found;
}

void enter(Inode &dp, const std::string &name, int64_t ino)
{
    if (name.empty())
        throw FsError("empty directory entry name");
    if (ino <= 0)
        throw FsError("directory entry for " + name + " has no i-number");
    if (lookup(dp, name) != 0)
        throw FsError("duplicate directory entry: " + name);

    DirEntry e;
    e.ino = ino;
    std::strncpy(e.name, name.c_str(), DIRSIZ);
    e.name[DIRSIZ] = '\0';

    //
    // The first unused slot, as namei() notes it in `eo' for a subsequent creat.
    // Reusing one keeps the directory from growing every time a file is replaced.
    //
    int64_t slot = -1;
    scan(dp, [&](int64_t ent, const DirEntry &probe) {
        if (probe.ino != 0)
            return false;
        slot = ent;
        return true;
    });

    put(dp, slot >= 0 ? slot : entry_count(dp), e);
}

bool unlink(Inode &dp, const std::string &name)
{
    int64_t slot = -1;

    scan(dp, [&](int64_t ent, const DirEntry &e) {
        if (e.ino == 0 || !name_equals(e.name, name))
            return false;
        slot = ent;
        return true;
    });

    if (slot < 0)
        return false;

    //
    // v7 clears the i-number and leaves the slot in place.  It does not shrink the
    // directory or coalesce with the neighbours -- there is nothing to coalesce,
    // the entries being fixed length.  The NAME is cleared too, which v7 does not
    // bother with; leaving it would make a dead slot indistinguishable from a live
    // one in a raw dump, and fsck has to read those dumps.
    //
    DirEntry dead;
    dead.ino = 0;
    put(dp, slot, dead);
    return true;
}

void make_empty(Inode &dp, int64_t parent_ino)
{
    dp.size  = 0;
    dp.dirty = true;

    DirEntry dot;
    dot.ino = dp.number;
    std::strcpy(dot.name, ".");
    put(dp, 0, dot);

    DirEntry dotdot;
    dotdot.ino = parent_ino;
    std::strcpy(dotdot.name, "..");
    put(dp, 1, dotdot);

    //
    // Two entries, 48 bytes.  NOT one block: see the header comment.
    //
    dp.size  = 2 * DIRENTSZ;
    dp.dirty = true;
}

} // namespace dir
