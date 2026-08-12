#
# /usr/bin/man -- 4.3BSD's man(1), and the first program on this image that reads /usr/man.
# Task C25b (../README.md).
#
# v7 HAD a man and this is not it: v7's is a shell script around nroff, and there is no
# nroff on this machine and never will be.  The source is RetroBSD's src/cmd/man/man.c,
# which is Berkeley's 1987 C rewrite.
#
# IT DOES NOT FORMAT ITSELF: it finds a page and runs one formatter over it, ../manview
# (task C25a), piped into /bin/more for a terminal.  FORMATTER in man.c is the whole of what
# it knows about formatting -- it was /bin/cat until C25a and is manview now, and nothing
# else in the file changed.  That is what the display path was arranged for.
#
# /usr/bin AND NOT /bin, which is the one placement here that is not this tree's habit --
# /bin holds the user commands and /usr/bin the ten natively built toolchain programs.  v7
# put man in /usr/bin, the shell's default path is `:/bin:/usr/bin' (../sh/msg.c), and the
# page says where it lives.  The ls /bin expectations in kernel/test/console.ini and
# session.expected are therefore untouched by this program.
#
# NO `LIBS' KEYWORD AND NONE NEEDED: stdio, three string functions and access(2), so
# b6_prog()'s -lc -lruntime is the whole link line (../../scripts/BesmCross.cmake).
#
# NOT `PURE': one man runs at a time and NMAGIC would only buy a shared text segment.
# NOT setuid: it opens what its caller could open itself.
#
# NO DATA FILE.  The section list is a table in man.c and the pages are already staged by
# B6_STAGE_MAN, so the only new line in kernel/test's ROOTFS_FILES is the program.
#
# The b6sim cases are in test/, and its CMakeLists.txt states the three things they cannot
# reach -- the default /usr/man, the environment, and everything after system(3).  README.md
# has the rest.
#
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(man DEST usr/bin/man SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/man.c)

add_subdirectory(test)
