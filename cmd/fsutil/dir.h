//
// Directories: fixed four-word entries, one i-number and three words of name.
//
// THIS IS NOT A PORT of the RetroBSD original's fs_inode_by_name().  That function
// is ~290 lines handling lookup, create, delete and link at once, and every one of
// them is shaped by a variable-length entry with a reclen and a namlen: slots get
// coalesced on delete, an entry gets extended to absorb its neighbour, a new name
// is squeezed into the slack of an old one.  A v7 entry is FIXED length and none
// of that applies.  What is here is written from kernel/nami.c:145-176 instead,
// which is the code that has to be able to read the result.
//
// THREE CONSEQUENCES OF THE FIXED ENTRY.
//
//   The directory works in ENTRY NUMBERS, not byte offsets.  DIRPB entries tile a
//   block exactly, so the block number and the slot within it are a shift and a
//   mask.  nami.c makes a point of this: the code it replaced masked a BYTE offset
//   with a 512-byte block's mask against a 3072-byte block, and so re-read the
//   block every 512 bytes and indexed from the wrong base in between.
//
//   A deleted entry is one with d_ino == 0.  It is not removed and nothing is
//   coalesced; the slot stays and the next create reuses it.
//
//   THE DIRECTORY'S SIZE IS NOT BLOCK-ROUNDED.  It is exactly
//   nentries * DIRENTSZ bytes.  The BSD source rounds up to a block in three
//   places because its variable-length entries need the slack; rounding here would
//   make namei() walk up to 126 zero slots on every lookup and would break
//   empty-slot reuse, since the scan stops at i_size.
//
#ifndef B6FSUTIL_DIR_H
#define B6FSUTIL_DIR_H

#include <functional>
#include <string>

#include "inode.h"

//
// Word offsets within a struct direct, include/sys/dir.h.
//
enum : int {
    DE_INO   = 0,
    DE_NAME  = 1, // .. 3, six chars to a word
    DE_WORDS = DIRWORDS,
};

static_assert(DE_NAME + DIRSIZ / NBPW == DE_WORDS, "the name must fill the rest of the entry");

struct DirEntry {
    int64_t ino           = 0;
    char name[DIRSIZ + 1] = {}; // NUL-terminated for the host's convenience only
};

namespace dir {

//
// Read entry `ent' out of an open directory.  Returns false past the end.
//
bool get(Inode &dp, int64_t ent, DirEntry &out);

//
// Overwrite entry `ent'.  The directory must already be long enough to hold it.
//
void put(Inode &dp, int64_t ent, const DirEntry &in);

//
// Every entry in turn, including the empty ones -- fsck wants those.  The callback
// gets the entry number so it can rewrite the slot.
//
void each(Inode &dp, const std::function<void(int64_t, const DirEntry &)> &fn);

//
// namei()'s inner loop: find `name', return its i-number, or 0.
//
int64_t lookup(Inode &dp, const std::string &name);

//
// Add an entry, reusing the first empty slot if there is one and extending the
// directory if there is not.  Refuses a duplicate name.
//
void enter(Inode &dp, const std::string &name, int64_t ino);

//
// Mark an entry unused.  The slot stays where it is, as v7 leaves it.
//
bool unlink(Inode &dp, const std::string &name);

//
// Make `dp' an empty directory: `.' and `..', and nothing else.  The caller owns
// the link counts.
//
void make_empty(Inode &dp, int64_t parent_ino);

//
// v7 truncates a name to DIRSIZ without complaint, so two names sharing an 18-char
// prefix are THE SAME NAME on this filesystem.  Callers building an image from a
// host directory tree should say so rather than let it happen quietly.
//
inline bool name_is_truncated(const std::string &name)
{
    return name.size() > size_t(DIRSIZ);
}

} // namespace dir

#endif // B6FSUTIL_DIR_H
