# /usr/lib/atrun -- Unix v7 atrun.  Task C21 (../README.md); ../at/README.md is the account,
# and at.1 is where this program is documented -- it has no page of its own.
#
# /usr/lib because at.1 says so: it is not a command a user types.  Its periodic caller is
# /etc/cron, which arrived with task C22 and runs it every five minutes.
#
# NO test/ HERE (../README.md §9): it reads a DIRECTORY, which b6sim cannot, and every path
# it names is the image's -- under the simulator /usr/spool/at would be the build machine's.
# kernel/test/multi is the assertion.
#
# NOT SETUID: it needs suser() for the setuid(2) that drops to the job's owner, so it must be
# run by root -- and a bit here would let anyone hand a file of their choosing to that same
# code path.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(atrun DEST usr/lib/atrun SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/atrun.c)
