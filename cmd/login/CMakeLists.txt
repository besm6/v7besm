# /bin/login -- Unix v7 login(1), built natively for the BESM-6.
#
# The second of kernel task 29b's two: a cmd/ subdirectory that is NOT a host
# tool.  b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over login.c, links
# it `crt0.o login.o -lc -lruntime' against the libc built in ../../lib, and stages the result
# as build/rootfs/bin/login.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# THE DESTINATION IS bin/login because ../getty/getty.c execs that path, as v7's does.
#
# NOT SETUID, and the reason is worth keeping next to the build rule rather than only in the
# manifest: getty is init's child and already root, so the privilege this program spends is
# privilege it was handed.  The v7 reference tree ships /bin/login 0755 for the same reason.
# A setuid login would additionally let any user re-run it from a shell, which is cmd/su's
# job (task C6) and not a door to open early.  It IS, however, the first program on this image
# to spend that privilege on purpose -- setgid()/setuid() to whoever answered the prompt --
# and README.md is the account.
#
# IT REACHES FURTHER INTO libc THAN ANYTHING ELSE IN cmd/: crypt, getpwnam/setpwent/endpwent,
# getpass, ttyname, ttyslot and stdio, three of which had never been called by anything on the
# image.  getpass in particular was carrying a fat-pointer bug of exactly the kind ../README.md
# §2 describes until the termcap port found it (lib/libc/README.md).
#
# NO b6sim TESTS, and there is no test/ subdirectory to add.  ../README.md §9: that world has
# no terminal (its ioctl is an unconditional success that changes nothing), no way to feed
# stdin, and no /bin/sh to exec -- and getpass() would sit waiting for a typist.  It is tested
# where the terminal is, in ../../kernel/test/login.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(login DEST bin/login SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/login.c)
