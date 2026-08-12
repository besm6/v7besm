# /bin/passwd -- Unix v7 passwd(1), built natively for the BESM-6.
#
# Task C6 (../README.md), and the only program on this image that WRITES /etc/passwd.  SETUID
# ROOT: the file is 0644 and root's, so nothing unprivileged could put a new hash in it.
# The bit is one line of ../../scripts/root.manifest and nothing else (../README.md §8).
#
# It rewrites through /etc/ptmp and copies BACK over /etc/passwd with creat(), because this
# kernel has no rename(2).  passwd.c's header is the account, including the /etc/ptmp leak
# that is v7's and is left alone.
#
# NO b6sim TESTS, and there is no test/ subdirectory -- and here §9's rule is not a
# limitation but a HAZARD.  getpwent() under that harness opens the literal /etc/passwd,
# which is the BUILD MACHINE's; a case that got as far as the rewrite would be asking b6sim
# to creat() the developer's password file.  It is tested in ../../kernel/test/accounts, and
# the oracle is a round trip: the account has no password, passwd gives it one, the session
# logs out, and the next login is ASKED for it.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(passwd DEST bin/passwd SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/passwd.c)
