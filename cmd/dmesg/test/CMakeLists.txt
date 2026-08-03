# cmd/dmesg/test -- dmesg(1) under b6sim.  Task C8.
#
# THE WHOLE PROGRAM IS ASSERTABLE HERE, which is unusual for something whose subject is
# kernel state, and it is b6sim's imitation kernel that makes it so (doc/Aout_Simulator.md
# SS7, cmd/sim/kernel.cpp).  Everything in that image is either a real value or a zero, with
# ONE exception, and the exception was put there for this program: msgbuf carries a fixed
# banner, because 128 zero bytes would make a working dmesg print nothing and a broken one
# print nothing too.  So the .expected below is a literal, and it is a literal of the whole
# output -- this port having dropped the date line that would otherwise have made it a
# projection (see ../dmesg.c).
#
# It also pins the fat-pointer arithmetic, which is the one thing in the program that can be
# subtly wrong: b6sim stores msgbufp at byte 20 of the ring, i.e. word 3 offset 2, so a
# ptrword()-only cursor would start two characters late and print `SM-6 Unix (b6sim)' with
# the banner's first two letters at the far end of the wrap.  A word-aligned banner would
# have hidden that; twenty bytes does not divide by six.
#
# WHAT NEEDS THE BOOT is that a REAL printf reached the ring -- kernel/test/inspect greps the
# boot diagnostics out of it.

b6_progtest(dmesg banner)
