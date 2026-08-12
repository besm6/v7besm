# /bin/rev -- Unix v7 rev(1), built natively for the BESM-6.
#
# One of task C5a's six (../README.md), and the only one of them that is not a faithful port:
# it reverses UTF-8 sequences where v7 reversed bytes.  README.md beside it is the account,
# rev.c's header the short form, and rev.1.umm was rewritten rather than corrected.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over rev.c, links it
# `crt0.o rev.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/rev.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(rev DEST bin/rev SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/rev.c)

add_subdirectory(test)
