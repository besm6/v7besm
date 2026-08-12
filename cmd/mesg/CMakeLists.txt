# /bin/mesg -- Unix v7 mesg(1), built natively for the BESM-6.
#
# Task C6 (../README.md).  A stat() and a chmod() on ttyname(2), and the other half of
# cmd/write: it is what decides whether a write(1) is refused.
#
# NOT SETUID, and this one is worth stating twice.  chmod(2) is gated on owner()
# (kernel/sys4.c), so the caller must own the terminal node -- which is exactly what
# cmd/login's chown() arranges, before it drops privilege.  A setuid mesg would let any user
# silence any terminal on the machine, which is the opposite of what the program is for.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9: ttyname(2) under that
# harness scans the BUILD MACHINE's /dev and the chmod() would land on the build machine's
# terminal.  It is tested in ../../kernel/test/multi.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(mesg DEST bin/mesg SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mesg.c)
