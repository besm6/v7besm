# The b6sim half of date's tests: what can be asserted with the host servicing the calls.
#
# THAT IS ONLY THE CONVERSION FAILURES.  Printing a time asks the host what time it is, and
# setting one goes nowhere -- b6sim's stime() is a no-op that reports success
# (cmd/sim/syscall.cpp) -- so every reproducible case here is one that never reaches
# stime(2) at all.  The real program, moving the real clock, is kernel/test/utils.

b6_progtest(date badconv)
b6_progtest(date badmonth)
b6_progtest(date badhour)
