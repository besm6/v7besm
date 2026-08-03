# /etc/iostat -- Unix v7 iostat(1M), built natively for the BESM-6.  Task C8.
#
# /etc AND NOT /bin, as quot beside it: v7's page is section 1M and v7 put the binary in
# /etc.  Unlike quot this one needs no privilege at all -- kctl(2) is unprivileged and it
# opens no device -- so the placement is v7's convention rather than a permission.  The
# side effect is the same and is worth knowing: the two hard-coded `ls /bin' expectations
# (kernel/test/console.ini and kernel/test/session.expected) do not have to grow for it.
#
# NOT SETUID, and there is nothing for it to be setuid to.
#
# THE DISK COLUMNS ARE NOT HERE, and iostat.c's header is the account: dk_busy is written
# by nothing, so kernel/ksym.c withholds it along with dk_numb[] and dk_wds[], and the
# HD/FD/CD half of v7's report has no numbers behind it.  kernel/TODO.md task 38.
#
# iostat.1m is the manual page, corrected in place; nothing installs it, it is read with
# `nroff -man iostat.1m'.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(iostat DEST etc/iostat SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iostat.c)

add_subdirectory(test)
