# The library test programs that go on the TEST PACK, as /test/<name> -- /mnt/test/<name>
# once the kernel has mounted it.  They were /usr/test on the root image until the pack
# existed; ../../test.manifest's header is the account of why they moved.
#
# ONE LIST, TWO CONSUMERS, and they are configured in different directories: this one
# (lib/test/CMakeLists.txt) links and stages them into build/testfs/test/, and
# kernel/test/CMakeLists.txt names them in TESTFS_FILES -- the dependency list that makes
# editing a test source rebuild test.img.  A third consumer, ../../test.manifest, is a static
# file and says the same names by hand; nothing but the manifest's own grammar can be
# generated for it.
#
# THIS IS NOT THE SAME LIST as the b6_libtest() calls next door, and deliberately.  Two
# programs are in one world only:
#   spawn   runs under b6sim ONLY.  Its whole premise is that /bin/sh cannot be exec'd,
#           which is true on the host and false on the image.
#   shellt  runs on the image ONLY, and is the other half of that: system() and popen()
#           with a shell that really does start.
#   memt    runs on the image ONLY, and is not a libc test at all: it is the user-mode half
#           of the memory driver's (kernel/dev/mem.c, task 27), and this is where a program
#           can be run off /mnt/test by a real kernel for the price of one b6_libtest() call.
#   kctlt   IS NOT ONE OF THEM, and is worth naming for that: its subject is kctl(2) over
#           the kernel-variable table (kernel/kctl.c), which for a while made it image-only
#           like memt.  b6sim imitates a kernel now -- the table, the values behind it and
#           /dev/kmem (cmd/sim/kernel.h) -- so the same program runs in BOTH worlds against
#           the one .expected, and a disagreement between them means b6sim's respelled copy
#           of the guest struct layouts has parted company with the real one.
#   suidt   runs on the image ONLY, and is cmd/mkdir's and cmd/rmdir's (task C1a): it drops
#           to uid 7 and execs them, which is the only way to reach getxfile()'s ISUID branch
#           on a system whose every shell is root's.  There is no /bin/mkdir under b6sim.
#   unprivt runs on the image ONLY, and is cmd/ps's and cmd/df's: it drops to uid 7 and runs
#           both, which since KCTL_PSINFO and statfs(2) they allow.  Its first three verdicts
#           are the negative control -- /dev/kmem, /dev/mem and /dev/rmd0 must STILL refuse
#           that uid -- without which "ps printed a table" would be equally consistent with a
#           loosened device node.  b6sim has neither /bin/ps nor a kernel to change a uid.
#   curstty runs on the image ONLY, and is lib/libcurses': it reads and writes the console's
#           tty modes, and b6sim's ioctl is an unconditional no-op that changes nothing, so
#           the two harnesses could not share an expectation.
#   ttyt    runs on the image ONLY, and is the half pwent had to give up when kernel task 29b
#           put /etc/ttys on the image: ttyname(0), ttyslot() and getlogin() answered the same
#           in both worlds only while that file was missing.
#   dirt    runs on the image ONLY, and is the directory family's -- opendir(3) and its six
#           companions.  ITS b6sim FAILURE IS THE QUIET ONE OF THE SIX: the host's open() and
#           fstat() both SUCCEED on a directory and only read(2) refuses, so under the
#           simulator opendir() returns a perfectly good DIR and the first readdir() returns
#           NULL -- every directory in the world looks empty, and looks it in exactly the way
#           an empty one does.  Nothing in an expectation can tell those apart.
#   coret   runs on the image ONLY, and is the kernel's, not libc's: it is what closes the
#           `int'-vs-pointer sweep, and neither arm of it exists under b6sim -- there is no
#           core dump there at all, and ptrace(2) is refused with EPERM.  IT IS ALSO THE ONE
#           PROGRAM HERE WITH A SIMH TEST OF ITS OWN (kernel/test/core.ini), rather than
#           waiting for an image-side runner that no longer exists.
# So `spawn' is absent below and the other eight are present: 32 names here, 25 b6sim cases
# next door.
#
# NOR IS EVERY NAME HERE A libc TEST any more.  termcapt is lib/libtermcap's and cursest and
# curstty are lib/libcurses'.  All three NAME the database rather than letting tgetent() find
# it: $TERMCAP is set from <name>.args, the source tree's etc/termcap under b6sim and the
# image's /etc/termcap under the kernel -- the same file, so one .expected adjudicates both.
# That is deliberate and stays: b6sim serves /etc/termcap itself now (cmd/sim/etcfiles.cpp),
# so the default path would work too, but naming it is what exercises tgetent's $TERMCAP-is-a
# -file-name branch, which is half of what termcapt is for.
#
# The order is lib/test/CMakeLists.txt's registration order -- roughly the order libc was
# built up, so a failure early in the list is a failure in something everything after it
# depends on.  It was also the order the deleted image-side runner used.  The list survives
# because test.manifest and kernel/test/CMakeLists.txt both build the pack from it.
set(B6_LIBTEST_IMAGE
    hello vararg errno procs sbrkt malloct strings gen strtolt environ jmp headers
    stdiot printft scanft execs shellt memt kctlt suidt unprivt timet pwent ttyt dirt signals
    matht
    termcapt cursest curstty puret coret)
