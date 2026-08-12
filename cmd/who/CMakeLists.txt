# /bin/who -- Unix v7 who(1), built natively for the BESM-6.
#
# Task C6's first program (../README.md), and the first reader /etc/utmp has ever had: init
# creates that file and blanks records in it, login writes them, getlogin() reads the one
# record it is standing on, and until now nothing printed the table.
#
# NOT SETUID: /etc/utmp is 0644 and every field who prints is public.  It maps no uid to a
# name, so it does not even open /etc/passwd -- except on the `who am i' path with no
# terminal, where getpwuid() is the fallback guess.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9 is the rule and this
# is the plainest instance of it: /etc/utmp under that harness is the BUILD MACHINE's, as is
# the /etc/passwd getpwuid() would read and the /dev ttyname() would scan.  A fixture file
# could be handed to the `who file' form, but writing one means emitting BESM-6 six-byte
# words from the host, and the whole of task C6 is asserted where the terminals are anyway.
# It is tested in ../../kernel/test/multi, which has two people logged in at once.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(who DEST bin/who SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/who.c)
