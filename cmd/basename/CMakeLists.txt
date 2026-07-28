# /bin/basename -- Unix v7 basename(1), built natively for the BESM-6.
#
# One of task C2b's five (../TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over basename.c, links it
# `crt0.o basename.o -lc -lruntime' against the libc built in ../../lib, and stages the
# result as build/rootfs/bin/basename.  Added from inside the top-level libruntime guard,
# after lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID: it opens nothing and calls nothing gated.
#
# The reason to read basename.c before touching it is §2 of ../README.md: its suffix strip
# was the one place in task C2b where two char * values were compared, and on this machine
# that does not order them.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(basename DEST bin/basename SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/basename.c)

add_subdirectory(test)
