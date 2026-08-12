# /bin/ed -- Unix v7 ed(1), built natively for the BESM-6.
#
# Task C3 (../README.md), and the pivot of the whole userland plan: this is the first program
# on the image that can AUTHOR text.  Everything staged here before it was written on the
# build host.  b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over ed.c,
# links it `crt0.o ed.o -lc -lruntime' against the libc built in ../../lib, and stages the
# result as build/rootfs/bin/ed.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT PURE, and not a candidate for it: `PURE' buys a shared text segment for a program run
# by several processes at once (/bin/sh and lib/test/puret are the two), and it costs a
# page-aligned data segment.  Nothing runs two editors.
#
# NOT SETUID.  Nothing ed does needs privilege: it reads and writes files as its caller, and
# its temp file lives in /tmp at mode 0777.  See ../README.md SS8 for the general rule and
# README.md here for what this port did need.
#
# ../crypt/rotor.c IS -x, task C19, and ../crypt links the same file: one key schedule is
# what makes ed.1's and crypt.1's promise that the two interoperate a property of the build
# rather than a habit.  ../df and ../umount share ../mount/mtab.c the same way.  b6_prog()
# puts every object in ${name}.dir, so the two builds of it cannot collide.  The -I is for
# rotor.h -- and it is a §1 blind spot, b6_obj's header dependency being the system tree, so
# touch ed.c after changing that header.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(ed DEST bin/ed
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ed.c ${CMAKE_CURRENT_SOURCE_DIR}/../crypt/rotor.c
    CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/../crypt)

add_subdirectory(test)
