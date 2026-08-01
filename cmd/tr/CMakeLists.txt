# /bin/tr -- Unix v7 tr(1), built natively for the BESM-6.
#
# One of task C5b's seven (../TODO.md), and the cheapest: v7's tr is eight-bit clean already,
# its three tables being [256] as v7 wrote them.  tr.c's header is the account, including the
# one `&0377' of the eight that is load-bearing and the one write into a string literal that
# is not.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over tr.c, links it
# `crt0.o tr.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/tr.
#
# NOT SETUID: it opens nothing at all -- it is stdin to stdout.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tr DEST bin/tr SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tr.c)

add_subdirectory(test)
