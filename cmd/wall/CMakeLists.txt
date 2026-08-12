# /etc/wall -- Unix v7 wall(1M), built natively for the BESM-6.
#
# Task C6 (../README.md).  It reads /etc/utmp and forks one child per live terminal, a second
# apart, each writing the same message.
#
# THE DESTINATION IS etc/wall.  v7's section is 1M and its Makefile installs it in /etc,
# beside the other programs whose subject is the machine rather than a file -- /etc/mkfs,
# /etc/fsck, /etc/mount.  cmd/sh's default path reaches /bin only, so it is typed with its
# path, which is v7's arrangement rather than an oversight.  wall.1m.umm is the page.
#
# NOT SETUID, and the manual page states the reason from the other side: "the sender should
# be super-user to override any protections the users may have invoked."  That override is
# nothing but the ordinary permission check on opening a 0600 terminal, which admits its
# owner and root.  A setuid wall would hand the override to everybody and there would be no
# protection left to invoke.  Note the asymmetry with ../write, which makes the mesg(1)
# check in the program and therefore refuses even root: wall is the announcement that is
# allowed to be rude.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9: /etc/utmp there is
# the BUILD MACHINE's and the terminals it names are the build machine's.  It is tested in
# ../../kernel/test/multi, which is the only place with two terminals to broadcast to.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(wall DEST etc/wall SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/wall.c)
