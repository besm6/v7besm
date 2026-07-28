# The b6sim half of kill's tests: the argument paths, and ONLY the argument paths.
#
# NO CASE HERE MAY SEND A SIGNAL.  b6sim's kill() is the build machine's own
# (cmd/sim/syscall.cpp), so a pid in a case file would be a pid on the host -- and `kill 0'
# signals the whole process group, which is ctest's.  Every case below returns before
# reaching kill(2): two by way of the usage message, and `signoonly' by way of the empty
# pid list that a lone -signo leaves behind.  The real signal, between two real processes,
# is kernel/test/utils.

b6_progtest(kill usage)
b6_progtest(kill badpid)
b6_progtest(kill signoonly)
