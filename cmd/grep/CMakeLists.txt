# grep(1) -- task C5c, the fourteenth of cmd/TODO.md's text filters.
#
# The task the CCL bitmap belongs to: v7 packs a character class into 128 bits, and this
# machine's text is UTF-8 end to end (../README.md SS11), so it is 256 here.  That closed a
# wild store as well: the compile side never masked the character at all, so a pattern byte
# above 0177 stored past the sixteen-byte class into the compiled expression.  See
# README.md beside this file.
#
# ONE DELIBERATE DIVERGENCE, recorded there, in grep.c's header and in grep.1 as
# ../README.md SS10 requires: -b prints a BYTE OFFSET rather than v7's block number.
#
# grep.1 is the manual page for grep, fgrep and egrep together, as v7 wrote it; ../fgrep
# has no page of its own.  Nothing installs it -- it is read with `nroff -Kutf8 -man'.
#
# b6_prog() links crt0.o grep.o -lc -lruntime, stages the result into build/rootfs/bin/grep
# and registers rootfs_grep_size for the two address-space ceilings (../README.md SS6).
#
# NOT SETUID: grep opens files for reading as whoever ran it, which is exactly the
# privilege it should have.  ../README.md SS8.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(grep DEST bin/grep SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/grep.c)

add_subdirectory(test)
