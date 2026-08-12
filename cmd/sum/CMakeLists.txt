# /bin/sum -- Unix v7 sum(1), built natively for the BESM-6.
#
# One of task C5a's six (../README.md).  b6_prog() (../../scripts/BesmCross.cmake) runs the b6*
# toolchain over sum.c, links it `crt0.o sum.o -lc -lruntime' against the libc built in
# ../../lib, and stages the result as build/rootfs/bin/sum.
#
# NOT SETUID: it opens what the caller could open itself.
#
# The one thing this port changed is §4: the block count it prints is in 1024-byte blocks,
# like df's, du's, quot's and `ls -s', where v7's was in the PDP-11's 512-byte ones.  The
# checksum itself is unchanged and comes out bit for bit the same -- sum.c's header says why
# a 41-bit unsigned does not disturb a 16-bit rotate-add.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(sum DEST bin/sum SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sum.c)

add_subdirectory(test)
