//
// The `-D' verb: write one word of a filesystem image, wrongly and on purpose.
// See damage.h for why this exists and for the two rules it obeys that nothing
// else in the tool does.
//
#include "damage.h"

#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>

#include "dir.h"
#include "inode.h"
#include "superblock.h"

namespace damage {
namespace {

//
// A value.  Base 0, so a mode is written the way the rest of the world writes one
// -- 0100644 -- and a block number is written in decimal.  The whole string must
// be consumed: `-D sb.nfree=12x' is a typo and not the number 12.
//
int64_t parse_value(const std::string &spec, const std::string &s)
{
    if (s.empty())
        throw FsError("damage `" + spec + "': no value");

    char *end       = nullptr;
    const int64_t v = std::strtoll(s.c_str(), &end, 0);
    if (end == nullptr || *end != '\0')
        throw FsError("damage `" + spec + "': `" + s + "' is not a number");
    return v;
}

//
// An unsigned index out of a target name -- the N of `i5' or the K of `addr3'.
//
int64_t parse_index(const std::string &spec, const std::string &s)
{
    if (s.empty())
        throw FsError("damage `" + spec + "': no number");

    char *end       = nullptr;
    const int64_t v = std::strtoll(s.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || v < 0)
        throw FsError("damage `" + spec + "': `" + s + "' is not a number");
    return v;
}

struct Field {
    const char *name;
    int word;
};

//
// The superblock's named words, superblock.h.  s_free[] and s_inode[] are reached
// as free<K>/inode<K> rather than listed 480 times.
//
const Field sb_fields[] = {
    { "magic", SB_MAGIC },   { "bsize", SB_BSIZE }, { "inopb", SB_INOPB },   { "naddr", SB_NADDR },
    { "isize", SB_ISIZE },   { "fsize", SB_FSIZE }, { "time", SB_TIME },     { "tfree", SB_TFREE },
    { "tinode", SB_TINODE }, { "flock", SB_FLOCK }, { "ilock", SB_ILOCK },   { "fmod", SB_FMOD },
    { "ronly", SB_RONLY },   { "nfree", SB_NFREE }, { "ninode", SB_NINODE },
};

//
// The inode's, inode.h.  addr<K> for the eight disk addresses.
//
const Field di_fields[] = {
    { "mode", DI_MODE }, { "nlink", DI_NLINK }, { "uid", DI_UID },     { "gid", DI_GID },
    { "size", DI_SIZE }, { "atime", DI_ATIME }, { "mtime", DI_MTIME }, { "ctime", DI_CTIME },
};

//
// `prefix' followed by a number -- addr3, free17 -- yielding base + the number.
// Returns -1 when the name does not start with the prefix at all, so the caller
// can go on trying.
//
int indexed_field(const std::string &spec, const std::string &name, const char *prefix, int base,
                  int count)
{
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.compare(0, n, prefix) != 0)
        return -1;

    const int64_t k = parse_index(spec, name.substr(n));
    if (k >= count)
        throw FsError("damage `" + spec + "': " + prefix + " index " + std::to_string(k) +
                      " is out of range (0.." + std::to_string(count - 1) + ")");
    return base + int(k);
}

int lookup(const std::string &name, const Field *tab, size_t ntab)
{
    for (size_t i = 0; i < ntab; i++)
        if (name == tab[i].name)
            return tab[i].word;
    return -1;
}

//
// The one write.  Raw, masked to a word, and deliberately not through to_word():
// a value the format cannot hold is the point rather than an error.
//
void poke(Filesystem &fs, int64_t bno, int word, int64_t value, const std::string &what,
          std::ostream &log)
{
    if (bno < 0 || bno >= fs.image.nblocks())
        throw FsError("damage `" + what + "': block " + std::to_string(bno) +
                      " is outside the image");
    if (word < 0 || word >= BSIZEW)
        throw FsError("damage `" + what + "': word " + std::to_string(word) +
                      " is outside a block");

    Block b;
    fs.image.read_block(bno, b);

    const int64_t was = from_word(b[size_t(word)]);
    b[size_t(word)]   = Word(value) & WORD_MASK;
    fs.image.write_block(bno, b);

    log << "damage: " << what << ": block " << bno << " word " << word << ": " << was << " -> "
        << value << "\n";
}

} // namespace

void apply(Filesystem &fs, const std::string &spec, std::ostream &log)
{
    const size_t eq = spec.find('=');
    if (eq == std::string::npos)
        throw FsError("damage `" + spec + "': expected <target>=<value>");

    const std::string target = spec.substr(0, eq);
    const int64_t value      = parse_value(spec, spec.substr(eq + 1));

    const size_t dot = target.find('.');
    if (dot == std::string::npos || dot == 0)
        throw FsError("damage `" + spec +
                      "': expected sb.<field>, i<N>.<field>, e<N>.<K> or "
                      "b<N>.<K> before the `='");

    const std::string head = target.substr(0, dot);
    const std::string tail = target.substr(dot + 1);

    //
    // sb.<field> -- the superblock, always block SUPERB.
    //
    if (head == "sb") {
        int word = lookup(tail, sb_fields, sizeof(sb_fields) / sizeof(sb_fields[0]));
        if (word < 0)
            word = indexed_field(spec, tail, "free", SB_FREE, NICFREE);
        if (word < 0)
            word = indexed_field(spec, tail, "inode", SB_INODE, NICINOD);
        if (word < 0)
            throw FsError("damage `" + spec + "': no superblock field `" + tail + "'");

        poke(fs, SUPERB, word, value, target, log);
        return;
    }

    if (head.size() < 2)
        throw FsError("damage `" + spec + "': expected sb, i<N>, e<N> or b<N>");

    const int64_t n = parse_index(spec, head.substr(1));

    switch (head[0]) {
    //
    // i<N>.<field> -- one word of one inode.  itod()/itoo() do the arithmetic, so
    // this cannot drift from the i-list the rest of the tool writes.
    //
    case 'i': {
        if (n < 1 || n > fs.inode_count())
            throw FsError("damage `" + spec + "': inode " + std::to_string(n) +
                          " is outside the i-list (1.." + std::to_string(fs.inode_count()) + ")");

        int word = lookup(tail, di_fields, sizeof(di_fields) / sizeof(di_fields[0]));
        if (word < 0)
            word = indexed_field(spec, tail, "addr", DI_ADDR, NADDR);
        if (word < 0)
            throw FsError("damage `" + spec + "': no inode field `" + tail + "'");

        poke(fs, itod(n), itoo(n) * DI_WORDS + word, value, target, log);
        return;
    }

    //
    // e<N>.<K> -- the i-number of entry K of directory inode N.  bmap() finds the
    // block the way the kernel would; a hole means the entry does not exist yet.
    //
    case 'e': {
        if (n < 1 || n > fs.inode_count())
            throw FsError("damage `" + spec + "': inode " + std::to_string(n) +
                          " is outside the i-list (1.." + std::to_string(fs.inode_count()) + ")");

        const int64_t ent = parse_index(spec, tail);

        Inode dp;
        dp.get(fs, n);
        if (!dp.is_dir())
            throw FsError("damage `" + spec + "': inode " + std::to_string(n) +
                          " is not a directory");

        const int64_t bno = dp.bmap(ent / DIRPB, false);
        if (bno < 0)
            throw FsError("damage `" + spec + "': directory " + std::to_string(n) +
                          " has no entry " + std::to_string(ent));

        poke(fs, bno, int(ent % DIRPB) * DE_WORDS + DE_INO, value, target, log);
        return;
    }

    //
    // b<N>.<K> -- a raw word, for the damage no field name describes: a free-list
    // chain block's count, an indirect block's address, a directory's name.
    //
    case 'b':
        poke(fs, n, int(parse_index(spec, tail)), value, target, log);
        return;

    default:
        throw FsError("damage `" + spec + "': expected sb, i<N>, e<N> or b<N>");
    }
}

} // namespace damage
