# /etc/init -- the first NATIVE BESM-6 program in cmd/.
#
# Unlike every other cmd/ subdirectory, nothing here is compiled by the host compiler:
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over init.c and links
# it against the libc built in ../../lib, staging the result as build/rootfs/etc/init --
# the /etc/init of the root filesystem the kernel mounts.  See README.md.
#
# This directory is therefore added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/ and kernel/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(init DEST etc/init SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/init.c)
