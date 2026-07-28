# The b6sim half of tty's cases, and what they assert is the NEGATIVE answer.
#
# ttyname(3) has to scan /dev with read(2) on the directory (lib/libc/gen/ttyname.c), and
# b6sim's read is the host's, which refuses a directory.  So under this harness ttyname()
# answers NULL no matter what descriptor 0 is -- whether ctest handed the test a terminal or
# not -- and the two cases below are the two ways the program reports that: `not a tty' on
# standard output, or, with -s, nothing at all.  Both exit 1.
#
# The positive answer is kernel/test/utils.sh's, where the same binary prints /dev/console.
# Two harnesses giving opposite answers for a reason each states is what ../../README.md §9
# means by running a program in both worlds.

b6_progtest(tty notty)
b6_progtest(tty silent)
