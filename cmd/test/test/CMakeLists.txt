# The b6sim half of test's cases, and the largest set in cmd/ -- because this is the program
# the harness fits best.  test(1) prints nothing when it works: its whole answer is the exit
# status, which scripts/run-prog-test.sh asserts BEFORE it looks at the output, so a case is
# one .args line and one number.  The file predicates name files in this directory through
# @srcdir@, which the harness substitutes.
#
# WHAT IS NOT HERE, and where it is instead:
#
#   THE `[' SPELLING.  main() switches on argv[0], and b6_progtest runs the program by its
#   staged path -- build/rootfs/bin/test -- so no case here can enter through the other name.
#   The `] missing' diagnostic and the bracket form are asserted in kernel/test/utils.sh,
#   where the shell types `[' and the image's hard link supplies it.
#
#   AN EMPTY ARGUMENT.  run-prog-test.sh field-splits the .args line and performs no quote
#   removal, so `-z ""' would pass the two quote characters.  `-z abc' is the false side and
#   the empty side is not reachable from this harness.
#
# nottty IS deterministic here, which is the one thing about this world worth stating: the
# harness redirects the program's standard output into a file, and b6sim's gtty is the host's
# isatty (cmd/sim/syscall.cpp), so `-t' with no descriptor is always false.

b6_progtest(test numeq)
b6_progtest(test numne)
b6_progtest(test ne)
b6_progtest(test gt)
b6_progtest(test lt)
b6_progtest(test ge)
b6_progtest(test le)
b6_progtest(test streq)
b6_progtest(test strne)
b6_progtest(test strfalse)
b6_progtest(test nonempty)
b6_progtest(test zempty)
b6_progtest(test oneword)
b6_progtest(test notop)
b6_progtest(test andop)
b6_progtest(test andfalse)
b6_progtest(test orop)
b6_progtest(test paren)
b6_progtest(test length)
b6_progtest(test isfile)
b6_progtest(test nofile)
b6_progtest(test isdir)
b6_progtest(test notdir)
b6_progtest(test readable)
b6_progtest(test noread)
b6_progtest(test nonzero)
b6_progtest(test zerosize)
b6_progtest(test nottty)
b6_progtest(test noargs)
b6_progtest(test argexpected)
b6_progtest(test unknownop)
b6_progtest(test parenexpected)
