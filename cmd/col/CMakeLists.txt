# /bin/col -- Unix v7 col(1), built natively for the BESM-6.
#
# One of task C5b's seven (../README.md), and the one deliberate divergence in the task:
# v7's col steals bit 0200 of every stored character as the Model 37 Teletype's Greek
# half-shift flag and masks its input with `c &= 0177', which between them turn `привет' into
# `P?QP8P2P5Q'.  Both are gone.  README.md beside it is the account, col.c's header the
# short form, and col.1.umm carries it too (../README.md §10 asks for both).
#
# col does NOT need the file-oracle harness the task list expected it to: it opens no file at
# all -- getchar() in, putchar() out -- so its cases are the ordinary
# <case>.in/<case>.expected shape.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over col.c, links it
# `crt0.o col.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/col.
#
# NOT SETUID: it opens nothing at all -- it is stdin to stdout.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(col DEST bin/col SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/col.c)

add_subdirectory(test)
