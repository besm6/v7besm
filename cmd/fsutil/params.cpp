//
// Does fsutil.h still agree with the kernel about the on-disk format?
//
// THIS FILE EMITS NO CODE.  It exists so that the answer is a build failure rather
// than a corrupted image.  The constants in fsutil.h are hand-copied from
// include/sys/param.h -- they have to be, for the three reasons the header comment
// in fsutil.h gives -- and a hand copy drifts.  It has drifted before: INOPB went
// 8 -> 32 and NADDR 13 -> 8 in a single commit (bedbdf2), and sbcheck() in
// kernel/alloc.c exists precisely because an image built by a mkfs one generation
// out of step mounts perfectly well and reads every inode from the wrong offset.
//
// This is the ONLY translation unit that includes sys/param.h, and that isolation
// is deliberate: param.h defines NULL as 0 (param.h:93), which clashes with the
// C++ library's own definition under -Werror.  It is only safe here because
// nothing below this point uses the C++ library.
//
#include "fsutil.h"

//
// fsutil.h's values, captured under other names BEFORE param.h is included.
//
// This copy is not ceremony.  param.h's constants are MACROS, so from the moment
// it is included the token `NBPW' means param.h's 6 and there is no longer any
// spelling that reaches fsutil.h's constexpr -- not even `::NBPW', which the
// preprocessor rewrites just the same.  Reading them out first is the only way to
// have both values in scope at once, which is the entire job of this file.
//
namespace ours {
constexpr int nbpw     = NBPW;
constexpr int bsize    = BSIZE;
constexpr int bsizew   = BSIZEW;
constexpr int nindir   = NINDIR;
constexpr int nshift   = NSHIFT;
constexpr int nmask    = NMASK;
constexpr int naddr    = NADDR;
constexpr int nlevel   = NLEVEL;
constexpr int inopb    = INOPB;
constexpr int inoshift = INOSHIFT;
constexpr int inomask  = INOMASK;
constexpr int dirsiz   = DIRSIZ;
constexpr int dirwords = DIRWORDS;
constexpr int direntsz = DIRENTSZ;
constexpr int dirpb    = DIRPB;
constexpr int dirshift = DIRSHIFT;
constexpr int dirmask  = DIRMASK;
constexpr int nicinod  = NICINOD;
constexpr int nicfree  = NICFREE;
constexpr Word magic   = FS_MAGIC;
} // namespace ours

//
// param.h is the WHOLE kernel's tunable header, not just the filesystem's, so a
// handful of its macros collide with the host's.  Each is dropped explicitly
// rather than silenced with a -Wno- flag: if a future param.h grows another
// clash, the build names it.
//
//   NULL    param.h:93 spells it `0'; the C++ library has its own.
//   NSIG    the kernel's signal count (17), not the host's.
//   SIGIOT  the kernel's number; the host defines it as SIGABRT.
//
// The path is spelled out rather than reached through an -I, and that is
// deliberate.  include/ is the kernel's FULL system-header tree -- it has its own
// ctype.h, stdio.h, string.h -- so putting it on this library's include path
// shadows the host's headers and every C++ translation unit in the tool stops
// compiling.  (Tried; the failure is a wall of errors from inside libc++.)  One
// relative include reaches the one file that is wanted and poisons nothing.
#undef NULL
#undef NSIG
#undef SIGIOT
#include "../../include/sys/param.h"

//
// The geometry.  Each of these is a plain integer in param.h, so it compares
// directly.  itod(), itoo() and makedev() are NOT checked here: they carry casts
// to the kernel's daddr_t/ino_t/dev_t, which on the host are libc's types and mean
// something else.  Their arithmetic is covered by test/inode_test.cpp instead.
//
static_assert(ours::nbpw == NBPW, "NBPW disagrees with sys/param.h");
static_assert(ours::bsize == BSIZE, "BSIZE disagrees with sys/param.h");
static_assert(ours::bsizew == BSIZEW, "BSIZEW disagrees with sys/param.h");
static_assert(ours::nindir == NINDIR, "NINDIR disagrees with sys/param.h");
static_assert(ours::nshift == NSHIFT, "NSHIFT disagrees with sys/param.h");
static_assert(ours::nmask == NMASK, "NMASK disagrees with sys/param.h");

static_assert(ours::naddr == NADDR, "NADDR disagrees with sys/param.h");
static_assert(ours::nlevel == NLEVEL, "NLEVEL disagrees with sys/param.h");
static_assert(ours::inopb == INOPB, "INOPB disagrees with sys/param.h");
static_assert(ours::inoshift == INOSHIFT, "INOSHIFT disagrees with sys/param.h");
static_assert(ours::inomask == INOMASK, "INOMASK disagrees with sys/param.h");

static_assert(ours::dirsiz == DIRSIZ, "DIRSIZ disagrees with sys/param.h");
static_assert(ours::dirwords == DIRWORDS, "DIRWORDS disagrees with sys/param.h");
static_assert(ours::direntsz == DIRENTSZ, "DIRENTSZ disagrees with sys/param.h");
static_assert(ours::dirpb == DIRPB, "DIRPB disagrees with sys/param.h");
static_assert(ours::dirshift == DIRSHIFT, "DIRSHIFT disagrees with sys/param.h");
static_assert(ours::dirmask == DIRMASK, "DIRMASK disagrees with sys/param.h");

static_assert(ours::nicinod == NICINOD, "NICINOD disagrees with sys/param.h");
static_assert(ours::nicfree == NICFREE, "NICFREE disagrees with sys/param.h");

static_assert(ours::magic == FS_MAGIC, "FS_MAGIC disagrees with sys/param.h");

//
// ROOTINO and SUPERB are NOT checked here, though they look like they could be.
// param.h spells them ((ino_t)2) and ((daddr_t)1), and those typedefs are the
// kernel's own -- one 48-bit word -- from sys/types.h, which this file must not
// include.  Declaring them locally to make the macros expand would collide with
// libc's `ino_t' on any host that has already declared it, which is most of them.
// Their values are v7's and have not moved since 1979; test/create_test.cpp
// asserts the root directory really does land at inode 2.
//

//
// The relations the kernel's own headers assert about themselves, restated in the
// host's terms.  include/sys/filsys.h and include/sys/ino.h say these with
// _Static_assert over sizeof(); this tool cannot, because a host `int' is not a
// BESM-6 word -- which is the reason those headers must not be included here.
//
static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
static_assert(16 * INOPB == BSIZEW, "INOPB 16-word inodes must tile a block");
static_assert(DIRWORDS * DIRPB == BSIZEW, "DIRPB dirents must tile a block");
static_assert(DIRENTSZ == DIRWORDS * NBPW, "a dirent must be DIRWORDS words");
static_assert(DIRSIZ == (DIRWORDS - 1) * NBPW, "the name must fill the dirent's other words");
static_assert(1 + NICFREE <= BSIZEW, "a free-list chain block must fit one block");
static_assert(NINDIR == BSIZEW, "an indirect block holds one daddr_t per word");
static_assert(1 << NSHIFT == NINDIR, "NSHIFT must be LOG2(NINDIR)");
static_assert(1 << INOSHIFT == INOPB, "INOSHIFT must be LOG2(INOPB)");
static_assert(1 << DIRSHIFT == DIRPB, "DIRSHIFT must be LOG2(DIRPB)");
static_assert(NADDR > NLEVEL, "an inode needs at least one direct address");

//
// The superblock is exactly one block.  filsys.h asserts this over sizeof(); here
// it is the field count, which is the same statement about the same layout: 13
// scalars, s_nfree, s_free[], s_ninode, s_inode[], s_fill[17].
//
static_assert(13 + 1 + NICFREE + 1 + NICINOD + 17 == BSIZEW,
              "the superblock must be exactly one block -- see include/sys/filsys.h");
