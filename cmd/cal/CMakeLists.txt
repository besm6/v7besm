# /bin/cal -- Unix v7 cal(1), built natively for the BESM-6.
#
# Task C5f (../TODO.md), and the cheapest port in this directory since basename: no §2, no
# §3, no §4, no §11 and no floating point.  cal.c's header says why that is not luck -- every
# intermediate in jan1() was already bounded by a year clamped to 1..9999 -- and names the
# three things that were fixed: pstr() stepping off both ends of its row, a file-scope table
# cal() wrote into, and `%u' for a value that is an int here.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over cal.c, links it
# `crt0.o cal.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/cal.  It also registers rootfs_cal_size.
#
# cal.1.umm is the manual page; nothing installs it, it is read with `nroff -man'.
#
# NOT SETUID: it opens nothing at all.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(cal DEST bin/cal SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cal.c)

add_subdirectory(test)
