# /bin/wc -- Unix v7 wc(1), built natively for the BESM-6.
#
# One of task C5a's six (../README.md), and the first of the text filters.  b6_prog()
# (../../scripts/BesmCross.cmake) runs the b6* toolchain over wc.c, links it `crt0.o wc.o -lc
# -lruntime' against the libc built in ../../lib, and stages the result as build/rootfs/bin/wc.
#
# NOT SETUID: it opens what the caller could open itself.
#
# What is worth knowing about the port is in wc.c's header: the word test had to stop calling
# every byte above 0177 a delimiter, which is §11, and an unreadable file is an error now.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(wc DEST bin/wc SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/wc.c)

add_subdirectory(test)
