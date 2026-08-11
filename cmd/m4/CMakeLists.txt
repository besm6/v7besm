# m4(1) -- task C13, the third v7 command here built from a GRAMMAR and the first that is
# a grammar PLUS a hand-written translation unit.  m4y.y is eval's expression parser and
# m4.c the macro processor; b6_prog() compiles both into m4.dir and links crt0.o m4.o
# m4y.o -lc -lruntime.
#
# THE GENERATED SOURCE IS NAMED m4y AND NOT m4.  b6_yacc's second argument names both the
# working directory and the output file, and `m4' would make the parser's object m4.o --
# the same name m4.c's object already has.
#
# NO CFLAGS -I${M4Y_SRC_DIR}: m4.c names no token and includes no y.tab.h.  -vd is on all
# the same, and y.output is where the conflict count is read.  It is zero.
#
# NO HEADER OF THE PROGRAM'S OWN, so ../README.md SS1's build blind spot never arises.  The
# three globals the two units share -- evalval, pe, evalerr -- are extern in m4y.y and
# defined in m4.c, and one line of b6nm over the binary is the check.
#
# NOT PURE: m4 runs once and exits; a page-aligned data segment buys a sharing that never
# happens.  NOT SETUID: it writes /tmp as the invoking user (../README.md SS8).
#
# rootfs_m4_size is the ../README.md SS6 check.  README.md beside this file.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_yacc(M4Y_SRC m4y ${CMAKE_CURRENT_SOURCE_DIR}/m4y.y)
b6_prog(m4 DEST bin/m4 SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/m4.c ${M4Y_SRC})

add_subdirectory(test)
