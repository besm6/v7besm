# /bin/cmp -- Unix v7 cmp(1), built natively for the BESM-6.
#
# One of task C5a's six (../TODO.md).  b6_prog() (../../scripts/BesmCross.cmake) runs the b6*
# toolchain over cmp.c, links it `crt0.o cmp.o -lc -lruntime' against the libc built in
# ../../lib, and stages the result as build/rootfs/bin/cmp.
#
# NOT SETUID: it opens what the caller could open itself.
#
# What is worth knowing about the port is in cmp.c's header: the seven longs ../README.md §3
# credited this file with, and an isdigit() that could index _ctype_ out of range.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(cmp DEST bin/cmp SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmp.c)

add_subdirectory(test)
