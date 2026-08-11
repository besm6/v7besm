# expr(1) -- task C11, and the first v7 command on this image built from a GRAMMAR.
#
# b6_yacc() runs b6yacc -vd over expr.y in a working directory of its own
# (expr.yacc.dir) and hands back expr.c; b6_prog() compiles that into expr.dir and
# links crt0.o expr.o -lc -lruntime.  Two directories rather than one because y.tab.c,
# y.tab.h and y.output are fixed names -- ../../scripts/BesmCross.cmake says why.
#
# NO CFLAGS -I${EXPR_SRC_DIR}: expr.y is one file and includes no y.tab.h.  -vd is on
# all the same, and y.output is where the grammar's conflict count is read.  It is zero;
# README.md records that, since establishing it is half of what this task is for.
#
# THE HEADER BLIND SPOT OF ../README.md SS1 DOES NOT APPLY HERE.  b6_obj's dependency is the
# system header tree and would not see a change to a program's own header -- but expr.y is
# a DEPENDS of b6_yacc's custom command, so editing the grammar regenerates and recompiles.
#
# NOT PURE: expr runs once per command substitution and exits; a page-aligned data segment
# buys a sharing that never happens.  NOT SETUID: it opens no file at all (../README.md SS8).
#
# rootfs_expr_size is the ../README.md SS6 check, and it is the only one of the four ceilings
# a script can make: the 4,096-word stack under advance()'s recursion was measured by hand
# and is written into MAXDEPTH.  README.md beside this file.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_yacc(EXPR_SRC expr ${CMAKE_CURRENT_SOURCE_DIR}/expr.y)
b6_prog(expr DEST bin/expr SOURCES ${EXPR_SRC})

add_subdirectory(test)
