# /bin/uniq -- Unix v7 uniq(1), built natively for the BESM-6.
#
# One of task C5b's seven (../README.md).  uniq.c's header is the account: the unbounded gline()
# §6 asks every port to bound, the isdigit() that indexed off the end of libc's 129-entry
# class table, and the final line with no newline that v7 threw away.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over uniq.c, links it
# `crt0.o uniq.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/uniq.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(uniq DEST bin/uniq SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/uniq.c)

add_subdirectory(test)
