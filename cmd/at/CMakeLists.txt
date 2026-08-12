# /bin/at -- Unix v7 at(1).  Task C21 (../README.md); ./README.md is the account.
#
# NOT SETUID (../README.md §8), and it matters more here than usual: the spool copy is owned
# by whoever ran at, and that ownership is how ../atrun recovers the uid to run the job
# under.  A bit here would make every job root's.
#
# at.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and ../../root.manifest
# has carried its stanza since before the program did.  It documents ../atrun as well.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(at DEST bin/at SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/at.c)

add_subdirectory(test)
