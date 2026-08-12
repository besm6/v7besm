# /etc/update -- Unix v7 update(8), built natively for the BESM-6.  Task C20 (../README.md).
#
# /etc AND NOT /bin, which is v7's placement and update.8.umm's SYNOPSIS: it is not a command
# a user types.  The page is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and
# ../../scripts/root.manifest has carried its stanza since before the program did.
#
# NO test/ HERE: a program that does not terminate cannot be a b6_progtest case, and sync(2)
# under b6sim would flush the BUILD MACHINE (../README.md SS9).  kernel/test/multi is the
# assertion; README.md beside this file says what it asks and why that test is the one.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sync/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(update DEST etc/update SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/update.c)
