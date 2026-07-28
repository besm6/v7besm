# /bin/tty -- Unix v7 tty(1), built natively for the BESM-6.
#
# One of task C2b's five (../TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over tty.c, links it
# `crt0.o tty.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/tty.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID: fstat on the caller's own descriptor and read on /dev need no privilege.
#
# The program is a one-line call on ttyname(3), and the interesting thing about it is that
# THE TWO TEST WORLDS GIVE OPPOSITE ANSWERS -- deterministically, which is why it is tested
# in both.  ttyname() scans /dev with read(2) on the directory; the kernel allows that and
# b6sim's host read refuses it, so the answer is /dev/console under kernel/test/utils and
# `not a tty' under the cases in test/.  tty.c's header is the account.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tty DEST bin/tty SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tty.c)

add_subdirectory(test)
