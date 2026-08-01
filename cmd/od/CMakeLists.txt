# /bin/od -- Unix v7 od(1), built natively for the BESM-6.
#
# One of task C5b's seven (../TODO.md), and the one that had to be DESIGNED rather than
# ported: od's subject is the machine word, and a machine word here is 48 bits where v7's was
# 16.  README.md beside it is the account, od.c's header the short form, and od.1 was
# rewritten rather than corrected -- §10's `touch' rule -- because its DESCRIPTION said
# `interpret words' and the word it meant has stopped existing.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over od.c, links it
# `crt0.o od.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/od.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(od DEST bin/od SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/od.c)

add_subdirectory(test)
