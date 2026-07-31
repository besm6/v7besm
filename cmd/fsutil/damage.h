//
// Deliberate corruption of a filesystem image -- the `-D' verb.
//
// WHY A TOOL THAT BREAKS THINGS LIVES BESIDE ONE THAT BUILDS THEM.  check.cpp is
// the host's fsck and cmd/fsck is the guest's, and the only way to know that
// either one works is to hand it something broken and see what it says.  Before
// this file the recipes existed only as C++ inside test/check_test.cpp, which
// meant the guest program -- which runs on the other side of a simulator and
// cannot call any of it -- had nothing to be tested against.  Task C4d's brief
// says it in as many words: "Deliberately corrupt an image with b6fsutil and
// require both to report the same thing."
//
// THE TARGETS ARE SYMBOLIC, and that is the whole design.  A shell script could
// compute the byte offset of inode 5's link count itself -- itod(), itoo(), INOPB,
// DI_NLINK, NBPW -- but then the on-disk layout would have a second home in a test
// script, which is exactly what cmd/TODO.md forbids and what params.cpp exists to
// prevent.  Everything here resolves through the same SB_*, DI_* and DE_* offsets
// the rest of the tool marshals with, so a layout change breaks the build here too.
//
// TWO RULES THIS FILE OBEYS THAT NOTHING ELSE IN THE TOOL DOES.
//
//   It writes a RAW word, masked to WORD_MASK, rather than going through
//   to_word().  That conversion throws on a value outside 41 signed bits, which is
//   right everywhere else and wrong here: a corruption tool whose worst case is a
//   value the format can hold is not much of a corruption tool.
//
//   It never sync()s.  Filesystem::sync() rewrites block 1 from the in-core
//   mirror, so a damaged superblock word would be quietly undone on the way out --
//   the damage would appear to work and would not be there.  apply() reads and
//   writes blocks directly and closes without touching the superblock it did not
//   damage.
//
#ifndef B6FSUTIL_DAMAGE_H
#define B6FSUTIL_DAMAGE_H

#include <iosfwd>
#include <string>

#include "filesystem.h"

namespace damage {

//
// Apply one damage specification to an open, writable filesystem.  The syntax:
//
//   sb.<field>=<value>   a superblock word: magic bsize inopb naddr isize fsize
//                        time tfree tinode flock ilock fmod ronly nfree ninode,
//                        and free<K>/inode<K> for the two cached arrays
//   i<N>.<field>=<value> an inode word: mode nlink uid gid size atime mtime ctime,
//                        and addr<K> for K in 0..NADDR-1
//   e<N>.<K>=<value>     entry K of directory inode N: its i-number
//   b<N>.<K>=<value>     word K of block N, for damage no field name describes
//
// A value is decimal, or octal with a leading 0 -- a mode has to be writable the
// way the rest of the world spells one.  What was done is written to `log', since
// a test whose damage is invisible in its own transcript is a test nobody can read.
//
// Throws FsError on a specification this cannot parse or a target outside the
// volume.
//
void apply(Filesystem &fs, const std::string &spec, std::ostream &log);

} // namespace damage

#endif // B6FSUTIL_DAMAGE_H
