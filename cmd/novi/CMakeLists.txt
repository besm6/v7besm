# /bin/novi -- a full-screen editor by Dave W Plummer, written for 2.11BSD on a split-I/D
# PDP-11 (https://x.com/davepl1968/status/2080867382105710812).  Task C12 (../TODO.md).
#
# THE FIRST PROGRAM IN cmd/ THAT IS NOT A PORT OF A v7 COMMAND.  Everything else here
# started life in the v7 source tree and the work was carrying it to a 48-bit word-addressed
# machine; novi came from outside, was written for a split-I/D PDP-11 running 2.11BSD, and
# the work was the four primitives this kernel does not have.  README.md is the account.
#
# THREE SOURCES, and they divide the way the risk does: buffer.c is the disk-backed gap
# buffer and touches no terminal, terminal.c is the terminal and touches no document, novi.c
# is the screen and the key bindings.  Only the third has no b6sim half -- see test/.
#
# NOT PURE: nothing runs two editors at once, and a page-aligned data segment costs a page.
# ../ed/CMakeLists.txt says the same about ed for the same reason.
#
# NOT SETUID: novi reads and writes files as whoever ran it, and its scratch files are its
# own.  ../README.md SS8.
#
# NO LIBS BEYOND libc, and that is now a choice rather than a constraint.  novi emits
# hard-coded ANSI/VT100 and reads no terminal database; b6_prog() HAS the `LIBS' keyword
# lib/libcurses/README.md predicted, and cmd/more is using it to link -ltermcap.  What
# stands between novi and the same is novi's own terminal.c, not the build (../README.md).

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole system tree at once, as it is in lib/, kernel/, cmd/sed/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# AND novi's OWN TWO HEADERS, which that glob does not reach.  b6_obj's coarse dependency is
# the SYSTEM header tree, so a program whose constants live in headers of its own gets no
# dependency on them and editing one rebuilds nothing.  cmd/sed/CMakeLists.txt is where that
# cost an afternoon; sed had one such header and novi has two.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/buffer.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/terminal.h)

b6_prog(novi DEST bin/novi
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/novi.c
                ${CMAKE_CURRENT_SOURCE_DIR}/buffer.c
                ${CMAKE_CURRENT_SOURCE_DIR}/terminal.c)

add_subdirectory(test)
