# /bin/test -- Unix v7 test(1), built natively for the BESM-6.
#
# The program of task C2b (../README.md), and the one that changes what a shell SCRIPT can be:
# this shell has no built-in for it, so until now nothing on this machine could branch.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over test.c, links it
# `crt0.o test.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/test.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# IT GOES ON THE IMAGE TWICE, under one inode: ../../scripts/root.manifest links /bin/[ to /bin/test,
# which is the first hard link that manifest has ever carried.  Nothing in this file does
# that -- a link is not a staged file and b6_prog() knows nothing about it -- so the
# rootimg_link test in ../../kernel/test/CMakeLists.txt is what holds it.  See README.md.
#
# NOT SETUID, and it must not become so: -r and -w are an open(2) probe on the caller's
# behalf, and a setuid test would answer them for the wrong user.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(test DEST bin/test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test.c)

add_subdirectory(test)
