# The library test programs that go on the root disk image, as /usr/test/<name>.
#
# ONE LIST, TWO CONSUMERS, and they are configured in different directories: this one
# (lib/test/CMakeLists.txt) links and stages them, and kernel/test/CMakeLists.txt names
# them in ROOTFS_FILES -- the dependency list that makes editing a test source rebuild
# root.img -- and registers the ctest case that diffs each program's output after the
# kernel has run it.  A third consumer, ../../root.manifest, is a static file and says the
# same names by hand; nothing but the manifest's own grammar can be generated for it.
#
# THIS IS NOT THE SAME LIST as the b6_libtest() calls next door, and deliberately.  Two
# programs are in one world only:
#   spawn   runs under b6sim ONLY.  Its whole premise is that /bin/sh cannot be exec'd,
#           which is true on the host and false on the image.
#   shellt  runs on the image ONLY, and is the other half of that: system() and popen()
#           with a shell that really does start.
#   memt    runs on the image ONLY, and is not a libc test at all: it is the user-mode half
#           of the memory driver's (kernel/dev/mem.c, task 27), and this is where a program
#           can be run off /usr/test by a real kernel for the price of one b6_libtest() call.
#   kctlt   IS NOT ONE OF THEM, and is worth naming for that: its subject is kctl(2) over
#           the kernel-variable table (kernel/ksym.c), which for a while made it image-only
#           like memt.  b6sim imitates a kernel now -- the table, the values behind it and
#           /dev/kmem (cmd/sim/kernel.h) -- so the same program runs in BOTH worlds against
#           the one .expected, and a disagreement between them means b6sim's respelled copy
#           of the guest struct layouts has parted company with the real one.
#   suidt   runs on the image ONLY, and is cmd/mkdir's and cmd/rmdir's (task C1a): it drops
#           to uid 7 and execs them, which is the only way to reach getxfile()'s ISUID branch
#           on a system whose every shell is root's.  There is no /bin/mkdir under b6sim.
#   curstty runs on the image ONLY, and is lib/libcurses': it reads and writes the console's
#           tty modes, and b6sim's ioctl is an unconditional no-op that changes nothing, so
#           the two harnesses could not share an expectation.
#   ttyt    runs on the image ONLY, and is the half pwent had to give up when kernel task 29b
#           put /etc/ttys on the image: ttyname(0), ttyslot() and getlogin() answered the same
#           in both worlds only while that file was missing.
# So `spawn' is absent below and the other five are present: 29 names here, 25 b6sim cases
# next door.
#
# NOR IS EVERY NAME HERE A libc TEST any more.  termcapt is lib/libtermcap's and cursest and
# curstty are lib/libcurses', and they are here for the reason memt is: the disk image is the
# only place their /etc/termcap exists.  Under b6sim the two that run there read the same
# file out of the source tree, named by lib/test/<name>.args.
#
# The order is the order kernel/test/libtest.sh runs them in, which is the order
# lib/test/CMakeLists.txt registers them in: roughly the order libc was built up, so a
# failure early in the list is a failure in something everything after it depends on.
set(B6_LIBTEST_IMAGE
    hello vararg errno procs sbrkt malloct strings gen strtolt environ jmp headers
    stdiot printft scanft execs shellt memt kctlt suidt timet pwent ttyt signals matht termcapt
    cursest curstty puret)
