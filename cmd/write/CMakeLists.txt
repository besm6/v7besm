# /bin/write -- Unix v7 write(1), built natively for the BESM-6.
#
# Task C6 (../TODO.md), and the first program on this image to open a terminal it does not
# own.  It reads /etc/utmp for the recipient's line, opens /dev/<line>, and copies its
# standard input there until end-of-file.
#
# NOT SETUID, and the reason is the program's own subject.  The terminal nodes are 0622, so
# opening one needs no privilege at all; and write makes the mesg(1) check ITSELF, refusing
# on `(st_mode & 02) == 0' after the open has already succeeded.  A setuid write would
# defeat exactly the check it exists to honour.  ../wall is the other half of that pair and
# its CMakeLists says so from its end.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9: /etc/utmp there is
# the BUILD MACHINE's, ttyname(2) scans its /dev, the tty it would open is the operator's,
# and the `!' escape fork/execs the build machine's /bin/sh.  It is tested in
# ../../kernel/test/multi, where there are two terminals and two people to write between.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(write DEST bin/write SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/write.c)
