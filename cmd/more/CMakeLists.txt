#
# /bin/more -- Berkeley's pager (Eric Shienbrood, UCB), and /bin/less beside it.
# Task C27 (../TODO.md).
#
# THE SECOND PROGRAM HERE THAT IS NOT A PORT OF A v7 COMMAND.  There was no pager in
# the v7 source tree at all -- more(1) is 3BSD's -- so there is no row for it in
# ../TODO.md's table of v7 programs still to port, exactly as there is none for
# cmd/novi.  The source is RetroBSD's src/cmd/more/more.c; the older UCB lineage that
# sits in the git-ignored cmd/tmp was read and not built from.
#
# NO `LIBS' KEYWORD AND NONE NEEDED.  more keeps RetroBSD's local termcap stubs and
# writes hard-coded ANSI/VT100, so b6_prog()'s -lc -lruntime is the whole link line
# (../../scripts/BesmCross.cmake).  The keyword ../../lib/libcurses/README.md has
# predicted since that library landed is still unbuilt, and this is the second program
# that would have wanted a terminal database and gets by without one.  What it costs
# is that a terminal which is not ANSI cannot use more; more.1.umm says so.
#
# THE HELP TEXT IS COMPILED IN, so there is no /usr/lib/more.help: no data-file stanza
# in ../../root.manifest and nothing extra in kernel/test's ROOTFS_FILES.  Twenty-four
# lines that one command prints are not worth an inode and a staging rule.
#
# /bin/less IS A HARD LINK to this same inode, declared in ../../root.manifest and
# asserted by kernel/test's rootimg_link_more.  It is a second NAME and nothing else:
# nothing in more.c reads argv[0] -- upstream's `page' alias did and was deleted with
# tailequ() -- so `less' is not GNU less and does not pretend to be.
#
# NOT `PURE': nothing runs two pagers at once, and NMAGIC would only buy a shared text
# segment.  NOT setuid: it opens what its caller could open.
#
# The b6sim cases are in test/, and its CMakeLists.txt states the one structural thing
# they cannot reach -- with standard output not a terminal this program is cat(1), so
# no option has an observable effect.  README.md has the rest.
#
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(more DEST bin/more SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/more.c)

add_subdirectory(test)
