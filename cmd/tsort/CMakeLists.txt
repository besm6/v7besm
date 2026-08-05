# /bin/tsort -- Unix v7 tsort(1), built natively for the BESM-6.
#
# Task C5f (../TODO.md).  tsort.c's header is the account: index() colliding with libc's
# index(3) in the shape §1 calls most dangerous, since b6ld pulls an archive member only for
# a symbol still undefined; error() called with one argument from two places on the cyclic
# path, which is the case this program exists to diagnose; and `%s' with no field width into
# fifty bytes, for archive member names that may be 255 on this machine where a PDP-11
# archive's were 14.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over tsort.c, links it
# `crt0.o tsort.o -lc -lruntime' against the libc built in ../../lib, and stages the result
# as build/rootfs/bin/tsort.  It also registers rootfs_tsort_size -- which cannot see this
# program's real footprint, one malloc per node and one per edge with no ceiling anywhere
# (§6's fourth ceiling).
#
# tsort.1.umm is the manual page; manview(1) formats it on the image, and it is
# legible as it stands.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tsort DEST bin/tsort SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tsort.c)

add_subdirectory(test)
