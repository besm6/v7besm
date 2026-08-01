# fgrep(1) -- task C5c, with ../grep.
#
# THE ONE THING TO KNOW BEFORE TOUCHING IT: w[MAXSIZ] is the Aho-Corasick state table and
# a scalar `char' member occupies a whole word here, so v7's 6000 states are 30,000 words
# -- past the 28,672 the user address space allows (../README.md SS6) before a byte of text.
# MAXSIZ is 3000 and two _Static_asserts hold it there.  fgrep.c's header is the account.
#
# -b prints a BYTE OFFSET, as grep's does; v7 divided by a PDP-11 disk block here and by
# BSIZE in grep, so the two commands disagreed about the same match.
#
# NO MANUAL PAGE OF ITS OWN: ../grep/grep.1 documents grep, fgrep and egrep together, as
# v7 wrote it, and that is where this program's corrections went.
#
# NOT SETUID: it reads files as whoever ran it.  ../README.md SS8.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(fgrep DEST bin/fgrep SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/fgrep.c)

add_subdirectory(test)
