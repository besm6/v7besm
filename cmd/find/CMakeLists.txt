# /bin/find -- Unix v7 find(1), built natively for the BESM-6.
#
# Task C5f (../README.md), the largest of its seven, and THE ONLY ONE WITH NO test/ DIRECTORY
# -- the second program in cmd/ after mount/umount that b6sim can say nothing at all about.
# It reads directory descriptors, which the simulator refuses; it popen()s pwd, which would
# fork the BUILD MACHINE's shell; and -exec fork/execvp's, which under b6sim would run the
# build machine's programs on the build machine's files (../README.md §9).  Every assertion
# about this program is in ../../kernel/test/filters, and ./README.md says so out loud --
# C4f's rule that a deferral written down is the difference between a known gap and an
# unknown one.
#
# find.c's header is the account.  Three things are worth knowing from here: the parse tree
# was a union by REINTERPRETATION and a fat `char *' cast to a `struct anode *' floors to the
# word, so -name would have matched the wrong bytes; -cpio is deleted (a PDP-11 tape archive
# format, with a prompt for the next reel), which takes this file's only sbrk with it; and
# -size is in 1024-byte blocks, matching df, du, quot and ls -s.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over find.c, links it
# `crt0.o find.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/find.  It also registers rootfs_find_size -- which weighs the 600-word
# parse tree and CANNOT see the recursion, so descend()'s depth is counted by the program
# itself (§6's third ceiling).
#
# find.1.umm is the manual page; manview(1) formats it on the image, and it is
# legible as it stands.
#
# NOT SETUID: it opens and stats what the caller could, and -exec runs as the caller.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(find DEST bin/find SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/find.c)
