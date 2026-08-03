# cmd/nice/test -- nice(1) under b6sim.  Task C8.
#
# WHAT THIS HARNESS CAN AND CANNOT SEE.  b6sim does implement both calls nice(1) makes --
# nice(2) is the host's ::nice() and exec() really does replace the image (cmd/sim/syscall.cpp)
# -- but neither is ASSERTABLE here.  The priority nice(2) set is invisible to a program that
# has just exec'd away from it, and it takes a ps to read it back, which needs a kernel and a
# proc table; and a successful exec would print whatever the exec'd program prints, not
# anything of nice's.  So the three cases below are the three paths that END in nice(1):  the
# two argument failures and the diagnostic when the command cannot be found.
#
# THE PRIORITY ITSELF IS ASSERTED ON THE IMAGE, in kernel/test/inspect -- `nice -10 ps -l'
# reading its own NICE column back out of the proc table, which is the only place both halves
# exist at once.

b6_progtest(nice noargs)
b6_progtest(nice flagonly)
b6_progtest(nice noexec)
