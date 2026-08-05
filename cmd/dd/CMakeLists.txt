# /bin/dd -- Unix v7 dd(1), built natively for the BESM-6.
#
# Task C4b (../TODO.md), and the first program on this image whose reason for existing is a
# device rather than a file: dd is how anything gets copied to or from /dev/rmd0, which is
# why TODO.md puts it ahead of mkfs(1) and fsck(1).  b6_prog() (../../scripts/BesmCross.cmake)
# runs the b6* toolchain over dd.c, links it `crt0.o dd.o -lc -lruntime' against the libc
# built in ../../lib, and stages the result as build/rootfs/bin/dd.
#
# NOT SETUID, and it must not become one, for the reason df(1) and quot(1) are not: /dev/rmd0
# is mode 0600 because that one node is the contents of every file on the volume.  A setuid
# dd would be worse than a setuid df -- it would hand the filesystem out AND let anybody
# overwrite it.  dd.1.umm says so and ../README.md SS8 is the general rule.
#
# THE ONE DIVERGENCE IN THE COMMAND LANGUAGE is that ibs/obs default to BSIZE and the `b'
# suffix multiplies by BSIZE, where v7's were 512.  dd.c's header item 1 is the argument and
# dd.1.umm marks it Note:; the short version is that 512 is not a whole number of words, so with
# v7's default `dd if=/dev/rmd0 ...' could not reach the disk at all.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(dd DEST bin/dd SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dd.c)

add_subdirectory(test)
