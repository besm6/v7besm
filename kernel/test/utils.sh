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
# should be a coin toss.  time(1)'s three intervals are masked in the same place, and there
# is no way to choose those in advance at all.
#
# STDOUT AND STDERR ARE NEVER MIXED IN ONE COMMAND.  Everything C2a brought reports on
# stdout, which is v7's choice; time(1) alone reports on stderr, also v7's, so it is the one
# block below whose redirections name descriptor 2.  See the head of each source.
#
# TASK C2a WROTE THE FIRST HALF -- date, sleep and kill, the three that are about the
# machine rather than a file -- and task C2b appended the second: basename, tty, test with
# its second name `[', yes, and time.  The order is kill, sleep, date, then C2b's five.
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
#
#   HOW LONG ANYTHING TOOK.  time(1) is here for its exit status, its failed-exec diagnostic
#   and the fact that it forks, execs, waits and reads times(2) at all.  The numbers it
#   prints are masked; a test that asserted them would assert the speed of the host.

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

# ---- basename: two lines, because cmd/basename/test covers the argument grammar under
# b6sim and there is nothing here a kernel decides.  What this adds is that the IMAGE's copy
# runs -- the same bytes, on the machine that is going to use them.  The second line is the
# suffix strip, which is where the two char * comparisons used to be (cmd/README.md §2).
basename /bin/ls >>/tmp/utils.log
basename /usr/src/cmd/ls.c .c >>/tmp/utils.log

# ---- tty: THE ANSWER b6sim CANNOT GIVE.  ttyname(3) finds a name by reading /dev as a
# directory, which the simulator's host read(2) refuses, so cmd/tty/test asserts `not a tty'
# and this asserts the other side of the same program.  /dev/console and not /dev/tty because
# the match is by i-number: the shell running this script still has the console on descriptor
# 0 (a script argument goes to descriptor 10, cmd/sh/main.c), and only one entry in /dev is
# that inode.
tty >>/tmp/utils.log
tty -s
echo tty status $? >>/tmp/utils.log

# ---- test, and `[' -- THE POINT OF TASK C2b.  Everything above this line is a command with
# its arguments; this is the first block in any script on this image that BRANCHES, because
# until now there was nothing to branch on: this shell has no built-in test.
#
# `[' IS THE ASSERTION THAT THE HARD LINK IS THERE.  It is /bin/test's second name and
# nothing else on this image is a link at all (../../root.manifest); rootimg_link checks the
# inode on the host and this checks that the shell can find and run it.  The last of them
# leaves the `]' off on purpose, which is the one diagnostic only this spelling can produce.
#
# -t 0 IS TRUE HERE, for the reason the tty block above gives.
#
# AND THE 127 AT THE END IS NOT A TYPO.  test(1) exits 255 on a syntax error, as v7's did and
# as test.1 says -- but no caller on this machine can see that number.  A wait status is
# (code << 8) and it comes back through r12, a fifteen-bit index register, so every exit code
# from 128 up arrives truncated (lib/libc/sys/wait.S states it, and cmd/sh/service.c's
# await() repeats it): 255 << 8 loses its top bit and the shell reads 127.  That is the
# system's ABI and not this program's business -- it is why the two b6sim cases next door,
# where the host does the waiting, really do see 255.  cmd/test/README.md is the account.
if [ -f /etc/passwd ]
then
	echo test found /etc/passwd >>/tmp/utils.log
else
	echo test missed /etc/passwd >>/tmp/utils.log
fi
if [ -d /tmp -a -r /etc/motd ]
then
	echo test -d and -r agree >>/tmp/utils.log
fi
if [ x = y ]
then
	echo test says x is y >>/tmp/utils.log
else
	echo test says x is not y >>/tmp/utils.log
fi
[ -t 0 ]
echo test -t 0 status $? >>/tmp/utils.log
test 1 -eq 2
echo test 1 -eq 2 status $? >>/tmp/utils.log
[ 1 -eq 1 2>>/tmp/utils.log
echo bracket unterminated status $? >>/tmp/utils.log

# ---- yes, AND THE FIRST PIPELINE THIS IMAGE HAS EVER RUN.
#
# Nothing in kernel/test/ or cmd/sh/test/ had used `|' before this line, so the `echo hi |
# cat' below is here on purpose and is not padding: if pipes or SIGPIPE are broken, it fails
# first and separately, and a failure of the line after it is then really about yes(1).
#
# THAT SECOND LINE IS THE ONLY WAY TO TEST yes AT ALL.  The program has no terminating
# condition -- see cmd/yes/CMakeLists.txt on why it has no b6sim case -- so something has to
# stop it, and what stops it is the reader going away: the shell's `read' builtin takes one
# line and exits, the next write gets EPIPE and a SIGPIPE with it (kernel/pipe.c), and the
# default disposition kills the writer.  The shell says nothing about signal 13 (its sysmsg[]
# entry is deliberately null, cmd/sh/msg.c), so the console stays clean.
echo hi | cat >>/tmp/utils.log
yes hello | { read x; echo $x >>/tmp/utils.log; }

# ---- time: the first program here that forks, execs and MEASURES.
#
# THE THREE INTERVALS ARE NOT REPRODUCIBLE and run-utils.sh masks them, exactly as it masks
# date's seconds and for the same reason.  What is diffed as it stands is everything around
# them: the failed exec's diagnostic, which is strerror()'s and not v7's unchecked
# sys_errlist[] subscript, and the EXIT STATUS -- which is the child's, shifted down by
# time(1), so `time kill' reporting 2 is the assertion that a status passes through.
#
# stderr and stdout are never mixed in one command, which is why the first two lines below
# redirect only descriptor 2 (this program reports on the diagnostic output, as v7 put it)
# and the third throws both away.
time /no/such/command 2>>/tmp/utils.log
echo time status $? >>/tmp/utils.log
time sleep 1 2>>/tmp/utils.log
echo time status $? >>/tmp/utils.log
time kill >/dev/null 2>/dev/null
echo time status $? >>/tmp/utils.log

# Last, as in session.sh and files.sh: there is no update daemon here, so nothing else
# flushes the cache.
sync
echo utils done >/dev/console
