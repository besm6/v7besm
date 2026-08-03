# /etc/pstat -- Unix v7 pstat(1M), rewritten natively for the BESM-6.  Task C8.
#
# /etc AND NOT /bin, as iostat and quot beside it: section 1M, and v7 put the binary there.
# So the two hard-coded `ls /bin' expectations do not have to grow for it.
#
# NOT ROOT-ONLY, which is a correction to what ../TODO.md says: every table this program
# prints comes through kctl(2), and kctl(2) is unprivileged.  Only -u opens a device --
# /dev/kmem or /dev/mem, both 0640 and root's -- so seven of the eight modes work for
# anybody and that one does not.  pstat.c's header is the account.
#
# NOT SETUID, AND IT MUST NOT BECOME ONE, for -u's sake: /dev/mem is every process's memory.
#
# pstat.1m is the manual page, corrected in place; nothing installs it, it is read with
# `nroff -man pstat.1m'.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(pstat DEST etc/pstat SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/pstat.c)

add_subdirectory(test)
