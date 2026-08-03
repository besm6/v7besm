# /bin/ps -- Unix v7 ps(1), rewritten natively for the BESM-6.  Task C8.
#
# NOT A PORT, and ps.c's header is the account: v7's route to the data was nlist("/unix"), a
# hard-coded relocation fudge, a sequential read of /dev/mem and a u-area fetched off
# /dev/swap through PDP-11 memory management.  None of it has a counterpart here.  What is
# kept is the OUTPUT FORMAT and the manual page.
#
# /bin AND NOT /etc: ps is section 1, it is what a user types, and v7 put it there.  So the
# two hard-coded `ls /bin' expectations -- kernel/test/console.ini and
# kernel/test/session.expected -- had to grow for this one.
#
# NOT SETUID, AND IT MUST NOT BECOME ONE.  It works because /dev/kmem and /dev/mem are
# readable by root, and it says so and stops for anybody else, exactly as v7's did.  A
# setuid ps would hand every process's memory to everybody through a program that already
# knows the layout -- the argument ../quot/CMakeLists.txt makes about /dev/rmd0.
#
# ps.1 is the manual page, corrected in place; nothing installs it, it is read with
# `nroff -man ps.1'.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(ps DEST bin/ps SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ps.c)

add_subdirectory(test)
