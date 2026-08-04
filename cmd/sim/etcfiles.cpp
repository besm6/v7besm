//
// The target's own /etc, compiled into b6sim.
//
// b6sim hands every guest path straight to the host, and for almost everything that is the
// right answer: a file the guest names is a file the person running it meant.  Six paths are
// not like that.  A guest calling getpwuid(3) reaches lib/libc/stdio/getpwent.c, which opens
// the literal "/etc/passwd" -- as v7 does and as it must, that path being right on the image
// -- and under b6sim it would read THE BUILD MACHINE's password file.  Every name printed
// then becomes a property of whoever is building.  It is not a hypothetical: cmd/fsck's test
// used to mask fsck's OWNER= field out before diffing, cmd/quot could assert no per-uid
// report at all, and on macOS the `##' comment header at the top of /etc/passwd parses as an
// entry named `##' with uid 0, which beats root.
//
// WHY SERVING THESE IS NOT AN INVENTION.  kernel.h's rule is that every value b6sim
// genuinely knows is the real one and everything it has no counterpart for is left zero
// rather than made up.  These files pass that rule rather than bending it.  etc/group,
// etc/motd, etc/passwd, etc/rc, etc/termcap and etc/ttys are checked into this tree, staged
// into build/rootfs/etc by etc/CMakeLists.txt and written onto root.img by root.manifest --
// so a guest reading /etc/passwd under the booted kernel reads exactly the bytes below.
// This is the system the a.out was built for.  The HOST's /etc/passwd is the fiction.
//
// ALL SIX, and etc/rc especially.  A Linux build host HAS an /etc/rc and macOS does not, so
// leaving one out would leave b6sim's answer depending on who is building, which is the whole
// defect.  It is also the only rule cmd/sim/test/etc_test.cpp can state and check: every file
// in etc/ is in this table, so a seventh joining B6_ETC fails a test rather than quietly
// reaching the host.
//
// THREE OF THE SIX ARE VERBATIM AND THREE ARE ABRIDGED, and which is which is not a matter of
// taste:
//
//   passwd, group, ttys   verbatim, because they are PARSED.  getpwent(3), getgrent(3) and
//                         ttyslot(3) read fields out of them, and cmd/quot, cmd/fsck and
//                         lib/test/pwent diff what comes out against expectations that hold
//                         in BOTH worlds.  A byte's difference here is a wrong answer.
//   termcap               the five terminal entries, without the 34-line header comment.
//                         tgetent(3) skips a comment anyway, so nothing a guest can observe
//                         changed -- and every capability line is still the tree's.
//   motd, rc              abridged to a token.  Nothing under b6sim reads either: /etc/rc is
//                         run by init and there is no init here, and motd is printed by rc.
//                         A guest that CATS one sees less than the image would show it; that
//                         is the one place where the two worlds deliberately differ, and it
//                         is a place nothing asserts.
//
// cmd/sim/test/etc_test.cpp checks all three rules, one test each.
//
// WHAT IS STILL LEFT ZERO.  A served file has a size, a mode and an owner, all of them
// root.manifest's and all of them real.  It has no st_dev, no st_ino and no timestamps: there
// is no filesystem here to have them, and syscall.cpp's stat leaves them zero exactly as the
// imitation kernel leaves inode[] zero.
//
// THREE MECHANICAL POINTS about the transcription, all of them ways to lose a byte in
// silence:
//
//   * the first content byte sits on the SAME source line as R"ETC( , and )ETC"; on its own
//     line after the last.  A raw string keeps every byte, so a newline after the opening
//     delimiter would prepend one to the file.
//   * the sizes are sizeof(X) - 1, computed from the literal, so a size cannot drift from the
//     text it describes.  Only the text itself can drift, and that is what the test checks.
//   * .clang-format names no RawStringFormats, so a reformat leaves these bodies alone.  It
//     must stay that way: reformatting etc/termcap's escapes would corrupt the database and
//     nothing but the drift test would say so.
//
// The delimiter is ETC because `)ETC"' appears in none of the six.
//
#include "etcfiles.h"

#include <algorithm>

// etc/group -- the groups /etc/passwd's gids name.
static const char GROUP[] = R"ETC(other::1:root,daemon
sys::2:sys
bin::3:bin,guest
uucp::4:uucp
)ETC";

// etc/motd -- the message of the day, printed by /etc/rc.
static const char MOTD[] = R"ETC(
БЭСМ-6 UNIX Version 7
)ETC";

// etc/passwd -- the accounts: root, daemon, sys, bin, uucp, guest.
static const char PASSWD[] = R"ETC(root:.8Y/JOGhfuk1I:0:1::/:
daemon:x:1:1::/:
sys::2:2::/usr/sys:
bin::3:3::/bin:
uucp::4:4::/usr/lib/uucp:/usr/lib/uucico
guest::7:3::/usr/guest:
)ETC";

// etc/rc -- the boot script -- and the file a build host is most likely to have too.
static const char RC[] = R"ETC(# /etc/rc -- the boot script, run by /etc/init as sh /etc/rc.
date >/dev/console
)ETC";

// etc/termcap -- BSD's termcap.small, verbatim.
static const char TERMCAP[] = R"ETC(
cons25|ansi|ansi80x25:\
	:am:bs:NP:ms:pt:AX:eo:bw:ut:km:\
	:co#80:li#25:pa#64:Co#8:it#8:\
	:al=\E[L:cd=\E[J:ce=\E[K:cl=\E[H\E[J:cm=\E[%i%d;%dH:\
	:dc=\E[P:dl=\E[M:do=\E[B:bt=\E[Z:ho=\E[H:ic=\E[@:cb=\E[1K:\
	:nd=\E[C:rs=\Ec:so=\E[7m:se=\E[27m:up=\E[A:cr=^M:ta=^I:\
	:AF=\E[3%dm:AB=\E[4%dm:op=\E[39;49m:sc=\E7:rc=\E8:\
	:k1=\E[M:k2=\E[N:k3=\E[O:k4=\E[P:k5=\E[Q:k6=\E[R:k7=\E[S:k8=\E[T:\
	:k9=\E[U:k;=\E[V:F1=\E[W:F2=\E[X:K2=\E[E:nw=\E[E:ec=\E[%dX:\
	:kb=^H:kh=\E[H:ku=\E[A:kd=\E[B:kl=\E[D:kr=\E[C:le=^H:sf=\E[S:sr=\E[T:\
	:kN=\E[G:kP=\E[I:@7=\E[F:kI=\E[L:kD=\177:kB=\E[Z:\
	:IC=\E[%d@:DC=\E[%dP:SF=\E[%dS:SR=\E[%dT:AL=\E[%dL:DL=\E[%dM:\
	:DO=\E[%dB:LE=\E[%dD:RI=\E[%dC:UP=\E[%dA:cv=\E[%i%dd:ch=\E[%i%d`:\
	:mb=\E[5m:md=\E[1m:mr=\E[7m:me=\E[m:bl=^G:\
	:ve=\E[=S:vi=\E[=1S:vs=\E[=2S:
vt100|dec-vt100|vt100-am|vt100am|dec vt100:\
	:do=2\E[B:co#80:li#24:cl=50\E[H\E[J:sf=2*\ED:\
	:le=^H:bs:am:cm=5\E[%i%d;%dH:nd=2\E[C:up=2\E[A:\
	:ce=3\E[K:cd=50\E[J:so=2\E[7m:se=2\E[m:us=2\E[4m:ue=2\E[m:\
	:md=2\E[1m:mr=2\E[7m:mb=2\E[5m:me=2\E[m:\
	:is=\E>\E[?1;3;4;5l\E[?7;8h\E[1;24r\E[24;1H:\
	:if=/usr/share/tabset/vt100:nw=2\EE:ho=\E[H:\
	:as=2\E(0:ae=2\E(B:\
	:ac=``aaffggjjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||:\
	:rs=\E>\E[?1;3;4;5l\E[?7;8h:ks=\E[?1h\E=:ke=\E[?1l\E>:\
	:ku=\EOA:kd=\EOB:kr=\EOC:kl=\EOD:kb=\177:\
	:k0=\EOy:k1=\EOP:k2=\EOQ:k3=\EOR:k4=\EOS:k5=\EOt:\
	:k6=\EOu:k7=\EOv:k8=\EOl:k9=\EOw:k;=\EOx:@8=\EOM:\
	:K1=\EOq:K2=\EOr:K3=\EOs:K4=\EOp:K5=\EOn:pt:sr=2*\EM:xn:\
	:sc=2\E7:rc=2\E8:cs=5\E[%i%d;%dr:UP=2\E[%dA:DO=2\E[%dB:RI=2\E[%dC:\
	:LE=2\E[%dD:ct=2\E[3g:st=2\EH:ta=^I:ms:bl=^G:cr=^M:eo:it#8:\
	:RA=\E[?7l:SA=\E[?7h:po=\E[5i:pf=\E[4i:
xterm|linux|modern xterm:\
	:@7=\EOF:@8=\EOM:F1=\E[23~:F2=\E[24~:K2=\EOE:Km=\E[M:\
	:k1=\EOP:k2=\EOQ:k3=\EOR:k4=\EOS:k5=\E[15~:k6=\E[17~:\
	:k7=\E[18~:k8=\E[19~:k9=\E[20~:k;=\E[21~:kI=\E[2~:\
	:kN=\E[6~:kP=\E[5~:kd=\EOB:kh=\EOH:kl=\EOD:kr=\EOC:ku=\EOA:\
	:tc=xterm-basic:
xterm-basic|modern xterm common:\
	:am:bs:km:mi:ms:ut:xn:AX:\
	:Co#8:co#80:kn#12:li#24:pa#64:\
	:AB=\E[4%dm:AF=\E[3%dm:AL=\E[%dL:DC=\E[%dP:DL=\E[%dM:\
	:DO=\E[%dB:LE=\E[%dD:RI=\E[%dC:UP=\E[%dA:ae=\E(B:al=\E[L:\
	:as=\E(0:bl=^G:cd=\E[J:ce=\E[K:cl=\E[H\E[2J:\
	:cm=\E[%i%d;%dH:cs=\E[%i%d;%dr:ct=\E[3g:dc=\E[P:dl=\E[M:do=\E[B:\
	:ei=\E[4l:ho=\E[H:im=\E[4h:is=\E[!p\E[?3;4l\E[4l\E>:\
	:kD=\E[3~:kb=^H:ke=\E[?1l\E>:ks=\E[?1h\E=:le=^H:md=\E[1m:\
	:me=\E[m:ml=\El:mr=\E[7m:mu=\Em:nd=\E[C:op=\E[39;49m:\
	:rc=\E8:rs=\E[!p\E[?3;4l\E[4l\E>:sc=\E7:se=\E[27m:sf=^J:\
	:so=\E[7m:sr=\EM:st=\EH:\
	:ue=\E[24m:up=\E[A:us=\E[4m:ve=\E[?12l\E[?25h:vi=\E[?25l:vs=\E[?12;25h:
xterm-color|generic "ANSI" color xterm:\
	:Co#8:NC@:pa#64:\
	:AB=\E[4%dm:AF=\E[3%dm:ac=:op=\E[m:tc=xterm-r6:
)ETC";

// etc/ttys -- the two Consul lines a getty sits on.
static const char TTYS[] = R"ETC(10console
10tty1
)ETC";

//
// The table.  etc/CMakeLists.txt's B6_ETC order, which is alphabetical.
//
// One row per file, and held that way: a packed table would make a diff name a line rather
// than a file.  The marker must be the bare words or clang-format ignores it.
// clang-format off
static const EtcFiles::File TABLE[] = {
    { "/etc/group", GROUP, sizeof(GROUP) - 1 },
    { "/etc/motd", MOTD, sizeof(MOTD) - 1 },
    { "/etc/passwd", PASSWD, sizeof(PASSWD) - 1 },
    { "/etc/rc", RC, sizeof(RC) - 1 },
    { "/etc/termcap", TERMCAP, sizeof(TERMCAP) - 1 },
    { "/etc/ttys", TTYS, sizeof(TTYS) - 1 },
};
// clang-format on

//
// An EXACT match, as SimKernel::dev_minor() is.  "/etc//passwd", "/etc/./passwd" and a plain
// "passwd" after a chdir("/etc") all fall through to the host, and that is enough: the guest
// libc names the literal path (getpwent.c, getgrent.c, getpw.c, and termcap.c's E_TERMCAP),
// and so does cmd/passwd.  It is the same limit /dev/kmem has had since it was written.
//
const EtcFiles::File *EtcFiles::find(const std::string &path)
{
    auto it = std::find_if(std::begin(TABLE), std::end(TABLE),
                           [&path](const File &f) { return path == f.path; });
    return (it == std::end(TABLE)) ? nullptr : &*it;
}

int EtcFiles::count()
{
    return (int)(sizeof(TABLE) / sizeof(TABLE[0]));
}

const EtcFiles::File &EtcFiles::entry(int i)
{
    return TABLE[i];
}
