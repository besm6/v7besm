# /bin/yes -- yes(1), built natively for the BESM-6.
#
# One of task C2b's five (../TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over yes.c, links it
# `crt0.o yes.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/yes.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID: it writes to its own standard output and does nothing else.
#
# THERE IS NO test/ SUBDIRECTORY HERE, and that is the one thing about this program worth
# knowing.  Every other program of task C1 and C2 has b6sim cases beside it, but
# ../../scripts/run-prog-test.sh runs a program TO COMPLETION and diffs a finished file, and
# yes(1) has no terminating condition -- what stops it is a SIGPIPE from the reader at the
# far end of its pipe.  There is no argument list that makes it finish, so there is no case
# to write.  The assertion is in ../../kernel/test/utils.sh instead, where a pipeline can
# bound it: the shell's `read' builtin takes one line and exits, and the signal that follows
# is what stops this program.  cmd/kill/test/CMakeLists.txt states its own exclusion the same
# way; the difference is that this one leaves nothing behind to register.
#
# v7 shipped no manual page for yes -- ../README.md names it as one of the two commands
# undocumented anywhere in the v7 tree -- so yes.1.umm beside the source is new.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(yes DEST bin/yes SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/yes.c)
