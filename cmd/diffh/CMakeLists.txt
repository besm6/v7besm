# /usr/lib/diffh -- Unix v7 diffh(1), built natively for the BESM-6.
#
# Task C5f (../README.md), and THE ONLY PROGRAM OF THE TASK THAT DOES NOT GO IN /bin.  It is
# not a command a user types: diff -h reaches it with
#
#	execv("/usr/lib/diffh", args)
#
# passing its own argv, -h included -- so this program's main() looks for a `b' in the first
# argument and ignores every other letter, and the path here and the string in ../diff/diff.c
# are the same and have to stay so.  It is also the first entry this image has ever had in
# /usr/lib, which ../../root.manifest therefore has to create.
#
# WHY IT IS PORTED AT ALL.  ../README.md offered the choice -- port it or drop -h -- and the
# argument for porting is not completeness: diff prints `files too big, try -h' from noroom()
# when its vectors will not fit, and diffh's window is bounded by a constant (RANGE 30 lines)
# where diff's storage is bounded by the input.  On a 28,672-word address space (../README.md
# §6) that message is NEARER than it was on a PDP-11, so -h is the escape hatch of a machine
# that needs one more, not less.
#
# diffh.c's header names the four changes: a bare `return;' from int main, four `%ld's,
# an unbounded path built into char[100], and isspace() over a raw file byte.
#
# NO test/ DIRECTORY OF ITS OWN.  ../diff/test drives this program through `diff -h', which
# is the only way anything reaches it, and ../diff/test/run-diff-test.sh is where the -h
# cases live -- b6sim cannot exec /usr/lib/diffh (../README.md §9), so they are booted.
#
# There is no diffh.1: diff.1 covers it, as ../README.md records for the ten programs with no
# page of their own.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(diffh DEST usr/lib/diffh SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/diffh.c)
