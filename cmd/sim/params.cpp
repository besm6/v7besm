//
// Does kernel.h still agree with the kernel about its own tunables?
//
// THIS FILE EMITS NO CODE.  It exists so that the answer is a build failure rather than a
// simulator that quietly describes a machine nobody is running.  cmd/fsutil/params.cpp is
// the same file for the same reason and came first; read its header for the full argument.
//
// WHAT IT CAN AND CANNOT COVER.  The counts in kernel.h's `kparam' are reachable, because
// <sys/param.h> is #define-only and includes nothing.  The word offsets in `klayout' are
// NOT: they come from <sys/proc.h> and <sys/user.h>, which carry the kernel's dev_t,
// daddr_t and time_t -- typedefs that mean one 48-bit word there and something else here --
// and which therefore cannot be compiled on the host at all.  Those are guarded instead by
// lib/test/kctlt, which computes the same numbers from the real headers and runs in BOTH
// worlds against one .expected; a drifted offset fails under b6sim and passes on the image.
// That is a runtime guard rather than a compile-time one, and it is the reason kctlt is not
// IMAGEONLY.
//
// This is the SECOND translation unit in the tree to include a kernel header by relative
// path; cmd/fsutil/params.cpp is the first, and cmd/sim/CMakeLists.txt notes the absence of
// include/ from this target's -I path for the reason given there.  Nothing below this point
// may use the C++ library.
//
#include "kernel.h"

//
// kernel.h's values, captured under other names BEFORE param.h is included.
//
// This copy is not ceremony.  param.h's constants are MACROS, so from the moment it is
// included the token `NBPW' means param.h's 6, and there is no longer any spelling that
// reaches kernel.h's constexpr -- not `kparam::NBPW', which the preprocessor rewrites to
// `kparam::6'.  Reading them out first is the only way to have both values in scope at
// once, which is the entire job of this file.
//
namespace ours {
constexpr int nbpw    = kparam::NBPW;
constexpr int kreach  = kparam::KREACH;
constexpr int kend    = kparam::KEND;
constexpr int ubase   = kparam::UBASE;
constexpr int hz      = kparam::HZ;
constexpr int nproc   = kparam::NPROC;
constexpr int ntext   = kparam::NTEXT;
constexpr int ninode  = kparam::NINODE;
constexpr int nfile   = kparam::NFILE;
constexpr int nmount  = kparam::NMOUNT;
constexpr int nsc     = kparam::NSC;
constexpr int msgbufs = kparam::MSGBUFS;
constexpr int cmapsiz = kparam::CMAPSIZ;
constexpr int smapsiz = kparam::SMAPSIZ;

// The word offsets and element sizes, captured for the arithmetic below.
constexpr int p_words     = klayout::P_WORDS;
constexpr int u_words     = klayout::U_WORDS;
constexpr int u_comm      = klayout::U_COMM;
constexpr int text_words  = klayout::TEXT_WORDS;
constexpr int inode_words = klayout::INODE_WORDS;
constexpr int file_words  = klayout::FILE_WORDS;
constexpr int mount_words = klayout::MOUNT_WORDS;
constexpr int tty_words   = klayout::TTY_WORDS;
constexpr int map_words   = klayout::MAP_WORDS;
constexpr int dktime_n    = klayout::DKTIME_N;

// The kctl(2) interface itself.
constexpr int op_get     = kctlop::GET;
constexpr int op_set     = kctlop::SET;
constexpr int op_list    = kctlop::LIST;
constexpr int op_stat    = kctlop::STAT;
constexpr int ksymlen    = kctlop::KSYMLEN;
constexpr int flag_rd    = kctlop::FLAG_RD;
constexpr int stat_words = kctlop::STAT_WORDS;
} // namespace ours

//
// param.h is the WHOLE kernel's tunable header, so a macro of its own can collide with the
// host's.  The clashes are dropped explicitly rather than silenced with a -Wno- flag: if a
// future param.h grows another, the build names it.
//
//   major, minor, makedev   the host declares its own, as macros on glibc and as functions
//                           behind macros on macOS.  param.h's are the BESM-6 encoding and
//                           are the ones wanted here -- though nothing below uses them.
//
// NULL needs no #undef: param.h guards it with #ifndef, so whichever is seen first wins and
// the two agree.  (cmd/fsutil/params.cpp still carries the #undef from before that guard.)
//
#undef major
#undef minor
#undef makedev
#include "../../include/sys/kctl.h"
#include "../../include/sys/param.h"

//
// The interface.  <sys/kctl.h> includes nothing and defines no type of the kernel's, so it
// comes in whole and these compare directly.  KCTL_SET is checked like the rest even though
// nothing implements it: the number is reserved, and reserving a DIFFERENT number on the two
// sides would be worse than not reserving one.
//
static_assert(ours::op_get == KCTL_GET, "KCTL_GET disagrees with sys/kctl.h");
static_assert(ours::op_set == KCTL_SET, "KCTL_SET disagrees with sys/kctl.h");
static_assert(ours::op_list == KCTL_LIST, "KCTL_LIST disagrees with sys/kctl.h");
static_assert(ours::op_stat == KCTL_STAT, "KCTL_STAT disagrees with sys/kctl.h");
static_assert(ours::ksymlen == KSYMLEN, "KSYMLEN disagrees with sys/kctl.h");
static_assert(ours::flag_rd == KCTLF_RD, "KCTLF_RD disagrees with sys/kctl.h");

//
// struct kctlstat is three one-word fields.  sizeof() cannot say so here -- a host int is
// four bytes and a guest one is six -- so the field count is the same statement about the
// same layout, and it is what syscall.cpp's pack_word() calls depend on.
//
static_assert(ours::stat_words == 3, "struct kctlstat must be kc_addr, kc_size, kc_flags");

//
// The tunables.  Each is a plain integer in param.h and compares directly.
//
static_assert(ours::nbpw == NBPW, "NBPW disagrees with sys/param.h");
static_assert(ours::kreach == KREACH, "KREACH disagrees with sys/param.h");
static_assert(ours::kend == KEND, "KEND disagrees with sys/param.h");
static_assert(ours::ubase == UBASE, "UBASE disagrees with sys/param.h");
static_assert(ours::hz == HZ, "HZ disagrees with sys/param.h");
static_assert(ours::nproc == NPROC, "NPROC disagrees with sys/param.h");
static_assert(ours::ntext == NTEXT, "NTEXT disagrees with sys/param.h");
static_assert(ours::ninode == NINODE, "NINODE disagrees with sys/param.h");
static_assert(ours::nfile == NFILE, "NFILE disagrees with sys/param.h");
static_assert(ours::nmount == NMOUNT, "NMOUNT disagrees with sys/param.h");
static_assert(ours::nsc == NSC, "NSC disagrees with sys/param.h");
static_assert(ours::msgbufs == MSGBUFS, "MSGBUFS disagrees with sys/param.h");
static_assert(ours::cmapsiz == CMAPSIZ, "CMAPSIZ disagrees with sys/param.h");
static_assert(ours::smapsiz == SMAPSIZ, "SMAPSIZ disagrees with sys/param.h");

//
// The image has to hold what this file lays into it.  These are the assertions that would
// otherwise be discovered as a guest reading zeros off the end of a table.
//
static_assert(ours::ubase + ours::u_words <= ours::kreach,
              "the u-area must fit below the unmapped reach");
static_assert(ours::u_comm + 3 <= ours::u_words, "u_comm[DIRSIZ] must fit inside struct user");

//
// Everything kernel.h lays out has to fit under KEND, where the real kernel image ends.
// lib/test/kctlt requires every exported address to be inside (0, KEND), so a layout that
// overflowed would fail there -- but it would fail as a puzzling test result rather than as
// a build error, which is what this restates.
//
static_assert(010000 + ours::nproc * ours::p_words + ours::ntext * ours::text_words +
                      ours::ninode * ours::inode_words + ours::nfile * ours::file_words +
                      ours::nmount * ours::mount_words + ours::nsc * ours::tty_words +
                      (ours::msgbufs + ours::nbpw - 1) / ours::nbpw +
                      ours::cmapsiz * ours::map_words + ours::smapsiz * ours::map_words +
                      ours::dktime_n + 9 /* the nine one-word scalars */
                  < ours::kend,
              "the imitation kernel's variables must all fit below KEND");
