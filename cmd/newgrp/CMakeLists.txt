# /bin/newgrp -- Unix v7 newgrp(1), built natively for the BESM-6.
#
# Task C6 (../README.md).  SETUID ROOT, and the reason is narrower than su's: setgid(2) is
# allowed when u_rgid already matches or when suser() passes (kernel/sys4.c), and the whole
# point of the program is to move to a group that is not the caller's -- so only the second
# gate is available.  done() hands the privilege straight back with setuid(getuid()) before
# it execs the shell.
#
# The bit is one line of ../../root.manifest and nothing else (../README.md §8).
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9: /etc/group and
# /etc/passwd there are the BUILD MACHINE's, and every path in this program ends in an
# execl("/bin/sh") that would run the build machine's shell.  It is tested in
# ../../kernel/test/accounts, where the assertion is the GROUP OF A FILE created afterwards
# -- which cannot be seen from inside the guest at all (../README.md, task C1).

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(newgrp DEST bin/newgrp SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/newgrp.c)
