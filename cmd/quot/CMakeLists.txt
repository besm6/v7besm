# /etc/quot -- Unix v7 quot(1M), built natively for the BESM-6.
#
# The third of task C4a's three (../README.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over quot.c, links it
# `crt0.o quot.o -lc -lruntime' against the libc built in ../../lib, and stages the result
# as build/rootfs/etc/quot.  Added from inside the top-level libruntime guard, after lib/,
# and not with the other cmd/ subdirectories.
#
# /etc AND NOT /bin, which is where df and du beside it go.  quot is a section-1M program:
# it reads the raw device, so it needs root and is of no use to anybody else, and v7 put it
# in /etc for that reason.  /etc/getty is the precedent on this image.  A side effect worth
# knowing: the two hard-coded `ls /bin' expectations (kernel/test/console.ini and
# kernel/test/session.expected) did not have to grow for this program, only for the other
# two.
#
# NOT SETUID, and it must not become one.  /dev/rmd0 is mode 0600 because that one node is
# every file's contents -- a setuid quot would let anybody read any of them by way of a
# program that already knows the layout.  quot.1m.umm says it is root-only; ../README.md SS8 is
# the general rule.
#
# THE RAW-READ RULES ARE df's, not restated here: ../df/README.md is the account of the four
# alignment conditions physio() and mdstrategy() impose, and quot.c's header cites it.  What
# is worth knowing at this level is what obeying them BOUGHT -- v7's `struct dinode
# itab[256]' is gone, 4,096 words of bss, because one block is INOPB inodes and the aligned
# block buffer is the i-node table.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(quot DEST etc/quot SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/quot.c)

add_subdirectory(test)
