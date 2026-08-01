# fgrep(1) -- task C5c, with ../grep.
#
# THE ONE THING TO KNOW BEFORE TOUCHING IT: w[MAXSIZ] is the Aho-Corasick state table and
# a scalar `char' member occupies a whole word here, so v7's 6000 states are 30,000 words
# -- past the 28,672 the user address space allows (../README.md SS6) before a byte of text.
# MAXSIZ is 3000 and two _Static_asserts hold it there.  fgrep.c's header is the account.
#
# AND THE SECOND THING: v7's cfail() walked the trie through a 400-entry CIRCULAR queue in its
# own stack frame, with a bound test on one arm of the wrap arithmetic and none on the other.
# The unchecked arm is the one a chain takes, so one keyword of 450 characters wrote past the
# frame; and it was a second ceiling tighter than MAXSIZ -- 399 keywords whatever their length,
# with the identical diagnostic.  The queue is linear, MAXSIZ long and in bss now, which is a
# proof and not a bigger constant: every state is enqueued exactly once.  MAXSIZ is the only
# ceiling this program has, and the budget _Static_assert counts both arrays.
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
