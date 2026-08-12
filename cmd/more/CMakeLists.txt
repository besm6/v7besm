#
# /bin/more -- Berkeley's pager (Eric Shienbrood, UCB), and /bin/less beside it.
# Task C27 (../README.md).
#
# THE SECOND PROGRAM HERE THAT IS NOT A PORT OF A v7 COMMAND.  There was no pager in
# the v7 source tree at all -- more(1) is 3BSD's -- so it never had a row among the v7
# programs to port, exactly as cmd/novi never had one.  The source is RetroBSD's src/cmd/more/more.c; the older UCB lineage that
# sits in the git-ignored cmd/tmp was read and not built from.
#
# THE FIRST CALLER OF b6_prog()'s `LIBS' KEYWORD, which ../../lib/libcurses/README.md
# predicted from the day that library landed and which this task added
# (../../scripts/BesmCross.cmake).  more(1) had RetroBSD's local termcap stubs and wrote
# hard-coded ANSI/VT100 because -lc -lruntime was the whole link line and there was no
# hook for a third archive; it now links ../../lib/libtermcap and reads the /etc/termcap
# that ../../etc stages, so a terminal that is not ANSI can use it.  cmd/novi is the
# other program that wanted this and has not moved yet.
#
# LIBS EMITS ITS OWN ORDER, not the caller's -- [-lm] [-lcurses] [-ltermcap] ahead of
# -lc -- so naming libtermcap alone is right here and a program naming libcurses would
# have to name libtermcap too.
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

b6_prog(more DEST bin/more SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/more.c LIBS libtermcap)

add_subdirectory(test)
