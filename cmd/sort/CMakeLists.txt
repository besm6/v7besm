# /bin/sort -- Unix v7 sort(1), built natively for the BESM-6.
#
# Task C5d (../TODO.md), the sixteenth of the text filters and the first program here that
# claims the whole heap.  sort.c's header is the account: the five 256-byte tables v7 rotated
# by 128 so a signed char could index them, which on a machine with unsigned chars read up to
# 128 bytes past each of them; what -d and -i then have to mean for a byte above 0177, which
# is the fifth deliberate divergence after touch, rev, col and grep -b; the arena that has to
# leave stdio room to malloc, since a stream whose buffer it cannot find degrades to one
# syscall per byte and says nothing; and the `char **' cast over an array of `struct merg *',
# whose second job -- marking duplicates under -u -- is invisible at the call site.
#
# ./README.md is the long form.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over sort.c, links it
# `crt0.o sort.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/sort.  It also registers rootfs_sort_size, which is the §6 measurement
# this task was told to take early -- though what it weighs is const+text+data+bss, and the
# arena sort takes at run time is exactly what it cannot see.
#
# sort.1 is the manual page; nothing installs it, it is read with `nroff -Kutf8 -man'.
#
# NOT SETUID: it opens what the caller could open itself, and writes its temp files as the
# caller too.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(sort DEST bin/sort SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sort.c)

add_subdirectory(test)
