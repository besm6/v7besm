# /usr/lib/makekey -- Unix v7 makekey(8).  Task C19 (../README.md), and the second program
# on this image outside /bin, ../diffh being the first: it is not a command a user types.
#
# IT HAS NO CALLER ON THE IMAGE, deliberately.  v7's crypt(1) and ed(1) forked it because
# the DES code shipped separately; here crypt(3) is in libc, so ../crypt/rotor.c calls it
# directly and gets the same thirteen bytes for one call instead of a fork, a pipe, an exec
# and a wait.  This is still the filter makekey.8 describes, for anything a user writes.
#
# makekey.8.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and
# ../../root.manifest has carried its stanza since before the program did.
#
# NOT SETUID (../README.md §8): it transforms what it is handed and holds no secret of its
# own.  NO stdio, so it is a fraction of the size of a program that has one.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(makekey DEST usr/lib/makekey SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/makekey.c)

add_subdirectory(test)
