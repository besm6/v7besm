# /etc/utils -- task C2's small-utility set, run by the console shell.
#
# Grafted onto a copy of the image by run-utils.sh, not shipped on it.  files.sh is the
# model, and its two standing rules hold here too:
#
# WHAT MAY REACH THE LOG: names, diagnostics and numbers a program computed.  Nothing the
# clock decided on its own -- which for THIS script needs saying twice, since half of it is
# about the clock.  The rule is kept by SETTING the time before printing it: every date
# below prints an instant this script chose, and run-utils.sh masks the seconds field
# afterwards, because a second can pass between the stime(2) and the print and no test
# should be a coin toss.
#
# STDOUT AND STDERR ARE NEVER MIXED IN ONE COMMAND.  All three of these programs report on
# stdout, which is v7's choice; see the head of each source.
#
# WHAT IS DELIBERATELY NOT ASSERTED, and why:
#
#   date's `no permission' path.  stime(2) is gated on suser() and every shell here is
#   root's, so reaching it needs a process that has dropped its uid -- the lib/test/suidt.c
#   pattern -- which is a test about privilege and not about date.
#
#   sleep's DURATION.  The guest clock is not reproducible to the second (run-files.sh says
#   why at length), so `sleep 1' is asserted only by returning.  What the kill block below
#   asserts instead is stronger and costs nothing: `sleep 60' is killed, and if the signal
#   had not arrived the step budget in utils.ini would run out rather than the script
#   finishing.

echo utils begin >/tmp/utils.log

# ---- kill: the argument paths first, which reach kill(2) not at all.
kill >>/tmp/utils.log
kill xyz >>/tmp/utils.log

# ...and one that does: a pid no process can have on a machine with NPROC entries.  The
# message is strerror()'s (lib/libc/gen/strerror.c) for the ESRCH the kernel's kill()
# returns when its scan matched nothing.
kill -9 32000 >>/tmp/utils.log

# ---- kill: a real signal, between two real processes.
#
# $! is the shell's last background pid and `wait' sets $? to 0200+signo for a child that
# died on one (cmd/sh/service.c), so 143 IS the assertion that SIGTERM was delivered -- and
# the sleep is 60 seconds so that a kill which did nothing cannot be mistaken for one that
# worked.  The shell also prints "<pid> Terminated" for the dead job; that goes to the
# console, not into this log, and the pid in it is why.
sleep 60 &
kill $!
wait
echo kill status $? >>/tmp/utils.log

# ---- sleep: the argument paths, then a real interval.
sleep >>/tmp/utils.log
sleep 1x >>/tmp/utils.log
sleep 1
echo slept >>/tmp/utils.log

# ---- date: set the clock, and read back what was set.
#
# EVERY LINE HERE IS ALSO A CHECK ON date's OWN ARITHMETIC, because the weekday is computed
# from the seconds count and nothing else: get the day wrong by one and `Wed' becomes `Tue'.
# The five instants are chosen for what they catch, and the last three are the ones that
# would have been unreachable before -- v7's date wrote `year += 1900' flat and could name
# no year after 1999 at all (cmd/date/date.c).
#
#   1980-01-02  an ordinary date, and the one the image is left carrying
#   1999-12-31  the 19xx side of the two-digit year window
#   2000-02-29  the 20xx side, AND a leap day that exists only under the full Gregorian
#               rule -- which is dysize()'s, and v7's date called it with an absolute year
#   2000-03-01  the day after it: this is where the `month >= 3' leap adjustment reads
#               dysize() for the year being set, so it is the line that fails outright if
#               that call keeps v7's argument
#   2001-02-03  a year past the century, so the summing loop has to cross it correctly too
date 8001020304 >>/tmp/utils.log
date >>/tmp/utils.log
date -u >>/tmp/utils.log
date 9912310102 >>/tmp/utils.log
date 0002290102 >>/tmp/utils.log
date 0003010102 >>/tmp/utils.log
date 0102030405.06 >>/tmp/utils.log

# The conversion failures, which never reach stime(2) and so leave the clock where it is.
date notadate >>/tmp/utils.log
date 8013010000 >>/tmp/utils.log

# ...and back to the first instant, so that what the image carries away is a time this
# script chose rather than the last thing it happened to test.
date 8001020304 >>/tmp/utils.log

# Last, as in session.sh and files.sh: there is no update daemon here, so nothing else
# flushes the cache.
sync
echo utils done >/dev/console
