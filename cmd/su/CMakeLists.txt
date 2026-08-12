# /bin/su -- Unix v7 su(1), built natively for the BESM-6.
#
# Task C6 (../README.md).  SETUID ROOT, and cmd/login named it a task in advance: v7's login(1)
# page claims login "may be used at any time to change from one user to another", that use
# needs the bit, and this is the command for it.  ../login/CMakeLists.txt says the same from
# its end -- login is NOT setuid because its privilege is inherited from getty, and opening
# that door for a shell was left to this program.
#
# The bit itself is one line of ../../root.manifest and nothing else (../README.md §8):
# nothing in build/rootfs/ carries a mode.  It is the first program on this image to be
# setuid root for something that is not a directory operation -- mkdir, mv and rmdir are the
# other three -- and the first whose whole subject is the transition.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9: getpwnam() there
# reads the BUILD MACHINE's /etc/passwd, getpass() would sit waiting for a typist at a
# terminal that world has not got, and the execl() would run the build machine's /bin/sh.
# It is tested in ../../kernel/test/accounts.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(su DEST bin/su SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/su.c)
