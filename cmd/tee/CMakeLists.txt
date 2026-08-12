# /bin/tee -- Unix v7 tee(1), built natively for the BESM-6.
#
# One of task C5a's six (../README.md).  b6_prog() (../../scripts/BesmCross.cmake) runs the b6*
# toolchain over tee.c, links it `crt0.o tee.o -lc -lruntime' against the libc built in
# ../../lib, and stages the result as build/rootfs/bin/tee.
#
# NOT SETUID: it creates what the caller could create itself.
#
# The one program of the six with no stdio at all -- every byte goes through read(2) and
# write(2) -- and therefore the smallest.  tee.c's header lists the four changes; the one to
# know about is that its buffers were 512 bytes, which was a PDP-11 disk block and names
# nothing here (../README.md §4), and are BSIZE now.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tee DEST bin/tee SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tee.c)

add_subdirectory(test)
