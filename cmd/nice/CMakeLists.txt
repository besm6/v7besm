# /bin/nice -- Unix v7 nice(1), built natively for the BESM-6.
#
# The first of task C8's five (../README.md), and the only one that reads no kernel state at
# all.  b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over nice.c, links
# it `crt0.o nice.o -lc -lruntime' against the libc built in ../../lib, and stages the result
# as build/rootfs/bin/nice.  Added from inside the top-level libruntime guard, after lib/.
#
# NOT SETUID: nice(2) itself is what refuses a negative increment to a non-super-user
# (kernel/sys4.c), so there is nothing for a mode bit to buy here.
#
# nice.1.umm is the manual page; manview(1) formats it on the image.  v7's
# page covered nohup(1) too -- a shell script that is not on this image -- and this one says
# so in a Note instead of documenting it.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(nice DEST bin/nice SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/nice.c)

add_subdirectory(test)
