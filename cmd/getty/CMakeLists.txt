# /etc/getty -- Unix v7 getty(8), built natively for the BESM-6.
#
# One of task 29b's two (../../kernel/TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over getty.c, links it
# `crt0.o getty.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/etc/getty.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# THE DESTINATION IS etc/getty AND NOT bin/getty, and that is not a preference: cmd/init's
# `char getty[] = "/etc/getty"' is the path init execs, and nothing on this system looks
# anywhere else.  /bin/login, next door, is likewise the path THIS program execs.
#
# NOT SETUID.  init is process 1 and runs as root; this is its child and has no privilege to
# acquire.  The transition that matters happens one exec later, in login, which drops to the
# user -- see ../login/README.md.
#
# NO b6sim TESTS, and there is no test/ subdirectory to add.  ../README.md §9 lists what that
# world cannot do and this program needs three of them: a terminal whose modes are real
# (b6sim's ioctl is an unconditional success that changes nothing and its gtty zero-fills five
# words), a stdin the harness can feed, and a program that terminates.  getty does not
# terminate -- it execs -- and everything it does before that is ioctl.  It is tested where
# the terminal is, in ../../kernel/test/login.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(getty DEST etc/getty SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/getty.c)
