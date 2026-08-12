# /bin/mail -- Unix v7 mail(1).  Task C28 (../README.md); ./README.md is the account.
#
# SETUID ROOT (../README.md SS8), and the seventh program on this image to carry the bit.
# deliver() appends to another user's 0644 mailbox, chown(2)s a new one -- suser()-gated --
# and chmod(2)s the lock bit on a file it does not own -- owner()-gated.  None of the three
# is available to an ordinary user, and without them mail can only deliver to yourself.
#
# WHAT MAKES THAT SAFE is that mail gives the privilege straight back: setuid(getuid()) is
# the FIRST STATEMENT of printmail(), so the `!command' escape, `s file', -f and everything
# else the reader types run as the reader.  The window is deliver() alone.  That ordering is
# load-bearing; mail.c says so where it happens.
#
# NOT PURE: nobody runs two mail readers at once.
#
# mail.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and ../../root.manifest
# has carried its stanza since long before the program did.  This task adds the binary and
# the /usr/spool/mail directory it delivers into.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(mail DEST bin/mail SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mail.c)

add_subdirectory(test)
