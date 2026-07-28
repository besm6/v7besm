# The b6sim half of sleep's tests.
#
# The argument parse is the whole of what this program does on its own; the interval it
# then waits is libc's sleep(3), and only the booted kernel can prove that arrives -- see
# kernel/test/utils, and lib/test/signals.c for the layer below it.  `zero' is here because
# sleep(0) returns without arming an alarm at all, which is the one timing case that costs
# nothing to assert.

b6_progtest(sleep noarg)
b6_progtest(sleep badchar)
b6_progtest(sleep zero)
