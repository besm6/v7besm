# /etc/mkfs -- Unix v7 mkfs(1M), built natively for the BESM-6.
#
# Task C4c (../README.md): the first program here that can CREATE a filesystem, and the first
# that WRITES a raw device at all -- kernel/dev/md.c's mdwrite() was dead code until this
# one.  b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over mkfs.c, links
# it `crt0.o mkfs.o -lc -lruntime' against the libc built in ../../lib, and stages the
# result as build/rootfs/etc/mkfs.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# /etc AND NOT /bin, quot's precedent: mkfs is a section-1M program that writes the raw
# device, so it needs root and is of no use to anybody else.  A side effect worth knowing,
# as it was for quot: the two hard-coded `ls /bin' expectations (kernel/test/console.ini
# and kernel/test/session.expected) did not have to grow for this program.
#
# NOT SETUID, and it must not become one.  /dev/rmd0 is mode 0600 because that one node is
# every file's contents; a setuid mkfs would let anybody destroy all of them with one
# command.  mkfs.1m.umm says it is root-only; ../README.md SS8 is the general rule.
#
# THE RAW-WRITE RULES ARE df's four plus one, and README.md beside this file is the
# account: physio() refuses a transfer whose base is below u.u_tsize, so a raw write may
# never be sourced from the text segment.  It cannot fire for an FMAGIC program, which is
# what b6_prog() produces without PURE.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(mkfs DEST etc/mkfs SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mkfs.c)

add_subdirectory(test)
