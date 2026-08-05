#
# /usr/bin/manview -- the renderer for the manual pages.  Task C25a (../TODO.md).
#
# THE THIRD PROGRAM HERE THAT IS NOT A PORT OF A v7 COMMAND, after ../novi and ../more, and
# for a sharper reason than either: the thing v7 ran in this position was nroff, which is not
# in the v7 source tree at all and which ../TODO.md's "Not ported, and why" table refuses
# along with the whole typesetting suite.  The pages on this image are the dialect
# ../../doc/Manual_Page_Format.md describes and this is what formats them.
#
# WRITTEN FROM SCRATCH AND IN C, over ../man2umm/ummread.cpp's recognition rules.  That file
# says in its own header that a renderer links it and adds a back end; it cannot be linked
# here -- it is C++ with std::string and std::vector in every signature, and this is a
# cross-compiled a.out with 28,672 words of address space.  What crossed over is the
# grammar, function for function, and parse.c's header says where the two must agree.
#
# THREE SOURCES AND THE SPLIT IS THE PROGRAM.  parse.c reads the dialect and calls render.c,
# which knows about a terminal and nothing else; manview.c is the option parser and the file
# loop.  There is no document model between the two halves, which is the one thing this
# program does differently from the converter and the reason it fits.
#
# NO `LIBS' KEYWORD AND NONE NEEDED: stdio and four string functions, so b6_prog()'s
# -lc -lruntime is the whole link line (../../scripts/BesmCross.cmake).
#
# NOT `PURE': one page is being read at a time and NMAGIC would only buy a shared text
# segment.  NOT setuid: it opens what its caller could open itself.
#
# NO DATA FILE.  The attribute table is in render.c -- there is no terminal database on this
# image (../../lib/libcurses/README.md's unbuilt keyword again) and ../more's hard-coded
# ANSI is the precedent -- so the only new line in kernel/test's ROOTFS_FILES is the program.
#
# /usr/bin AND NOT /bin, beside ../man, which is what invokes it: v7 put man in /usr/bin,
# the shell's default path is `:/bin:/usr/bin' (../sh/msg.c), and the `ls /bin' expectations
# in kernel/test/console.ini and session.expected are therefore untouched by this program.
#
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(manview DEST usr/bin/manview
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/parse.c
                ${CMAKE_CURRENT_SOURCE_DIR}/render.c
                ${CMAKE_CURRENT_SOURCE_DIR}/manview.c)

add_subdirectory(test)
