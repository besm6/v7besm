# /etc/inspect -- the six inspection programs, run by the console shell.  Five are task C8's
# and the sixth is cmd/vmstat, which arrived with the driver instrumentation.
#
# Grafted onto a copy of the image by run-inspect.sh, not shipped on it.  filters.sh is the
# model and utils.sh the model for its mask; the standing rule of both holds here too:
#
# WHAT MAY REACH THE LOG: verdicts, labels, and numbers a program computed from something
# that does not move.  NOT a pid, NOT a kernel address, NOT a tick count, NOT the clock.
# That is a harder rule for this test than for any other in this directory, because the
# subject of all five programs IS the volatile state of the machine -- so nearly every line
# below is a COMPARISON made in the guest, and what is diffed is the verdict.  Only two
# things are masked on the host afterwards, and run-inspect.sh names them.
#
# WHAT ONLY A BOOT CAN SAY, program by program.  b6sim answers kctl(2) and the two memory
# devices out of an imitation kernel (doc/Aout_Simulator.md SS7), so all five have a b6sim
# half and it is the larger half.  What that half CANNOT have is PLURALITY: proc[] there
# holds one live entry, the guest itself, and inode, file, text, mount and sc are empty on
# purpose.  So:
#
#   * dmesg -- that a REAL kernel printf reached msgbuf.  Under b6sim the ring is a fixed
#     banner put there so the program has a line to print; here it is the four lines
#     startup() and iinit() actually printed at boot (kernel/machdep.c, kernel/main.c), in
#     the order they printed them, having been written a character at a time by putchar()
#     in kernel/dev/sc.c.
#
#   * ps -- more than one process, a process in state S, and a WCHAN worth printing.  Under
#     b6sim every run has exactly one row, always R, always with a blank WCHAN.
#
#   * ps and pstat -u -- THE /dev/mem PATH.  b6sim's one process has p_addr == uhome, so a
#     ps there always reads the live u-area at UBASE through /dev/kmem and NEVER takes the
#     other branch.  Section 7 takes init's ADDR out of a ps listing and hands it to
#     `pstat -u', which is a u-area at a p_addr that is not the caller's, above KREACH, off
#     /dev/mem.  That is the one assertion in this task that nothing else can make.
#
#   * iostat -- a systm and an idle column with numbers in them, and A DEVICE THAT MOVED.
#     b6sim bills every tick to dk_time[0], there being no kernel to charge system time to
#     and nothing idle, so `user' is 100.00 there and the other three never move; and it has
#     no block device at all, so all six of the MD and MB fields are structurally zero.
#
#   * vmstat -- interrupts, context switches and a runnable process.  The same argument once
#     more and at its sharpest: b6sim has one process, no 0500 vector and no scheduler, so
#     `in', `cs', `tr', `b' and `w' cannot move there however long it runs.
#
#   * pstat -- every number it exists to print: a live inode table, open files, a mounted
#     root, and a console that is open with carrier on.
#
#   * nice -- the priority it set, read back.  nice(1) execs away from the priority it just
#     changed, so only a ps against a real proc table can see it, and only here do both
#     halves exist at once.
#
# SECTION MARKERS: filters.sh's convention, because five programs in one log want a diff
# that says which one moved.

echo inspect begin >/tmp/inspect.log

# ---- 1.  dmesg: a real kernel printf reached the ring ---------------------------------
#
# startup() prints three lines and iinit() a fourth, about 96 characters into a 128-byte
# ring -- so it has NOT wrapped and dmesg prints all four in order, skipping the 32 slots
# that have never been written.  The numbers are masked on the host (they are a function of
# the machine's size and of this image's), the four labels are not.
#
# IF A FIFTH KERNEL printf IS EVER ADDED AT BOOT the ring wraps and this section starts
# reporting a clipped first line.  That is the signal, not a flake: fix the expectation and
# say in this comment what now fills the ring.
echo --1-dmesg-- >>/tmp/inspect.log
dmesg >/tmp/dmesg.out
wc -l </tmp/dmesg.out >>/tmp/inspect.log
cat /tmp/dmesg.out >>/tmp/inspect.log

# ---- 2.  ps: plurality, a sleeping process, and a WCHAN --------------------------------
echo --2-ps-- >>/tmp/inspect.log
# THE `sleep 2' IS NOT PADDING.  `sleep 30 &' returns the moment the shell has FORKED, and
# the child has not exec'd yet: a ps run immediately after it finds a process still called
# `sh', in state R, with the flags SLOAD|SSWAP and no WCHAN.  That is a correct picture of a
# process caught mid-exec and it is not the one this section is about.  Two seconds later the
# same slot is `sleep', asleep on a channel, which is what is wanted -- and the fact that ps
# shows both is worth knowing rather than working around silently.
sleep 30 &
slept=$!
sleep 2
ps -alx >/tmp/ps.out
sed -n 1p /tmp/ps.out >>/tmp/inspect.log
sed -n '2,$p' /tmp/ps.out >/tmp/psrows.out
n=`wc -l </tmp/psrows.out`
if test $n -ge 4
then echo "ps: four or more processes" >>/tmp/inspect.log
else echo "ps: only $n processes" >>/tmp/inspect.log
fi

# The state letter is column 4 of a long row: `%2o' then a space then states[p_stat].
ns=`grep -c '^...S' /tmp/psrows.out`
if test $ns -ge 1
then echo "ps: at least one process is asleep" >>/tmp/inspect.log
else echo "ps: nothing is asleep" >>/tmp/inspect.log
fi

# A BLANK WCHAN is eight spaces, and the TTY field puts a ninth in front of the terminal
# name -- so a running process shows the last digit of SZ followed by nine spaces, and a
# sleeping one does not.  Counting the blank ones and subtracting is the only shape of this
# question v7 grep can answer: it has no interval expressions, so a column cannot be named.
nb=`grep -c '[0-9]         [0-9?] ' /tmp/psrows.out`
if test $n -gt $nb
then echo "ps: some process is waiting on a WCHAN" >>/tmp/inspect.log
else echo "ps: every WCHAN is blank" >>/tmp/inspect.log
fi

# The four commands that must be there.  CMD is the last field, printed ` %.*s'.
grep -c ' init$' /tmp/psrows.out >>/tmp/inspect.log
grep -c ' sleep$' /tmp/psrows.out >>/tmp/inspect.log
grep -c ' ps$' /tmp/psrows.out >>/tmp/inspect.log

# The short listing, and that its own row is there.  ps exits 1 when it prints nothing, so
# the status is an assertion in itself.
ps >/tmp/ps2.out
echo ps status $? >>/tmp/inspect.log
sed -n 1p /tmp/ps2.out >>/tmp/inspect.log

# ---- 3.  ps <pid>: the selector, against a pid the shell already knows ------------------
echo --3-pssel-- >>/tmp/inspect.log
ps $slept >/tmp/ps3.out
echo ps status $? >>/tmp/inspect.log
wc -l </tmp/ps3.out >>/tmp/inspect.log
sed -n 's/.* //p' /tmp/ps3.out >>/tmp/inspect.log

# ---- 4.  nice: the priority, read back out of the proc table ---------------------------
#
# NICE is columns 29 to 33 of a long row, so twenty-eight characters precede it.  This is
# the one place both halves of nice(1) exist at once: the program execs away from the
# priority it just set, and only a ps against a real proc table can see it.
echo --4-nice-- >>/tmp/inspect.log
ps -l | grep ' ps$' | sed 's/^............................\(.....\).*/\1/' >>/tmp/inspect.log
nice -10 ps -l | grep ' ps$' | sed 's/^............................\(.....\).*/\1/' >>/tmp/inspect.log
nice -100 ps -l | grep ' ps$' | sed 's/^............................\(.....\).*/\1/' >>/tmp/inspect.log

# ---- 5.  iostat: columns that b6sim can never move -------------------------------------
echo --5-iostat-- >>/tmp/inspect.log
/etc/iostat >/tmp/io.out
sed -n '1,2p' /tmp/io.out >>/tmp/inspect.log
sed -n 3p /tmp/io.out >/tmp/iorow.out
# -i NAMES THE FOUR CPU PERCENTAGES ONE PER LINE, and that is why the verdict is taken from
# it rather than from the report above.  The six device fields now sit in front of them, and
# `%3d.%02d' has no leading space at 100.00 -- so a `user' of 100.00 runs into the words
# column ahead of it, and splitting the data line on whitespace would see one field where
# there are two.  -i has no such neighbour.
/etc/iostat -i >/tmp/ioi.out
nz=`grep -c -v '^  0\.00 ' /tmp/ioi.out`
if test $nz -ge 2
then echo "iostat: at least two CPU categories are non-zero" >>/tmp/inspect.log
else echo "iostat: only $nz CPU categories are non-zero" >>/tmp/inspect.log
fi
# AND THE DISK MOVED, which is the half of this program that did not exist until dev/md.c
# and dev/mb.c were made to keep dk_numb and dk_wds.  This machine booted off /dev/md0 and
# has been reading it ever since, so MD's words-per-second -- characters 13 to 18 of the
# data line, cut BY POSITION for the reason just given -- cannot be zero unless the driver
# is not counting at all.  Under b6sim all six device fields are structurally zero, which is
# what cmd/vmstat/test and cmd/iostat/test assert instead.
w=`sed 's/^............\(......\).*/\1/' /tmp/iorow.out | tr -d ' '`
if test $w -gt 0
then echo "iostat: the disk has moved words" >>/tmp/inspect.log
else echo "iostat: the disk has moved no words" >>/tmp/inspect.log
fi
/etc/iostat -t >/tmp/iot.out
sed -n '1,2p' /tmp/iot.out >>/tmp/inspect.log

# ---- 6.  pstat: the tables that are empty under b6sim and are not here ------------------
echo --6-pstat-- >>/tmp/inspect.log
/etc/pstat -i >/tmp/pi.out
ni=`sed -n 's/ active inodes$//p' /tmp/pi.out`
if test $ni -gt 0
then echo "pstat: the inode table is not empty" >>/tmp/inspect.log
else echo "pstat: the inode table is empty" >>/tmp/inspect.log
fi
/etc/pstat -f >/tmp/pf.out
nf=`sed -n 's/ open files$//p' /tmp/pf.out`
if test $nf -gt 0
then echo "pstat: the file table is not empty" >>/tmp/inspect.log
else echo "pstat: the file table is empty" >>/tmp/inspect.log
fi
/etc/pstat -m >/tmp/pm.out
sed -n 1p /tmp/pm.out >>/tmp/inspect.log
# The console: ISOPEN and CARR_ON, which is `  OC' in the eight state letters T W O C B A X H.
/etc/pstat -t >/tmp/pt.out
sed -n '1,2p' /tmp/pt.out >>/tmp/inspect.log
grep -c '  OC' /tmp/pt.out >>/tmp/inspect.log
# The paging store, whose three scalars are conf.c's and do not move.
/etc/pstat -s | sed -n 1p >>/tmp/inspect.log

# ---- 7.  pstat -u over /dev/mem: a u-area that is not the caller's ----------------------
#
# ADDR is columns 34 to 41 of a long row, so thirty-three characters precede it.  init is
# process 1 and is asleep in wait(), so its image is in core and its p_addr is NOT uhome --
# which makes this the /dev/mem branch, above KREACH, and the one thing in the whole of task
# C8 that b6sim cannot reach.  lib/test/memt.c is the rung below it.
echo --7-uarea-- >>/tmp/inspect.log
a=`grep ' init$' /tmp/psrows.out | sed 's/^.................................\(........\).*/\1/' | tr -d ' '`
/etc/pstat -u $a >/tmp/pu.out
echo pstat status $? >>/tmp/inspect.log
grep '^comm ' /tmp/pu.out >>/tmp/inspect.log
grep -c '^u-area at 74000' /tmp/pu.out >>/tmp/inspect.log
# ...and the live one, for contrast: it IS at UBASE and it belongs to pstat itself.
/etc/pstat -u 0 >/tmp/pu0.out
grep '^comm ' /tmp/pu0.out >>/tmp/inspect.log
grep -c '^u-area at 74000 (the live one)$' /tmp/pu0.out >>/tmp/inspect.log

# ---- 8.  vmstat: the columns b6sim is structurally unable to move ----------------------
#
# THREE THINGS ONLY A BOOT CAN SHOW.  b6sim has one process, no block device, no 0500 vector
# and no scheduler, so `b', `w', `md', `mb', `in', `tr' and `cs' are zero there and
# cmd/vmstat/test asserts exactly that.  Here they move, and that they move IS the assertion
# that the four counters kernel/intr.c, syscall.c, trap.c and slp.c now keep are live.
#
# NO NUMBER REACHES THE LOG.  Every one of them is a rate over a tick count; the comparisons
# are made here and only the verdicts are diffed, which is this file's standing rule.
echo --8-vmstat-- >>/tmp/inspect.log
/etc/vmstat >/tmp/vm.out
sed -n '1,2p' /tmp/vm.out >>/tmp/inspect.log
# THE INTERMEDIATE FILE IS NOT STYLE.  cmd/sh cannot run a FOUR-stage pipeline inside
# backquotes: three works, four takes a SIGILL in the shell itself, and the same four stages
# outside backquotes are fine.  It is not this task's bug and not this task's to fix; it is
# recorded in ../../cmd/sh/README.md, and this is one of the two places in the tree that
# would have hit it.  The columns are cut by POSITION in any case -- adjacent fields have no
# separator between them and can run together, exactly as in section 5.
sed -n 3p /tmp/vm.out >/tmp/vmrow.out
# `in', characters 34 to 38: the interrupt rate.  At HZ = 250 the free-running timer alone
# puts it near 250, so anything at all here says extintr() is being counted.
n=`sed 's/^.................................\\(.....\\).*/\\1/' /tmp/vmrow.out | tr -d ' '`
if test $n -gt 0
then echo "vmstat: interrupts are being counted" >>/tmp/inspect.log
else echo "vmstat: no interrupts have been counted" >>/tmp/inspect.log
fi
# `cs', characters 44 to 48: a machine with an init, a shell and a sleep switches processes.
n=`sed 's/^...........................................\\(.....\\).*/\\1/' /tmp/vmrow.out | tr -d ' '`
if test $n -gt 0
then echo "vmstat: processes are being switched" >>/tmp/inspect.log
else echo "vmstat: nothing has been switched" >>/tmp/inspect.log
fi
# `r', characters 1 and 2: at least this vmstat is runnable, and the swapper is not counted.
n=`sed 's/^\\(..\\).*/\\1/' /tmp/vmrow.out | tr -d ' '`
if test $n -ge 1
then echo "vmstat: something is runnable" >>/tmp/inspect.log
else echo "vmstat: nothing is runnable" >>/tmp/inspect.log
fi
# -p prints four more columns over the same partition; only its two headings reach the log.
/etc/vmstat -p >/tmp/vmp.out
sed -n '1,2p' /tmp/vmp.out >>/tmp/inspect.log
# -s: the LABELS, every number stripped.  A cumulative counter is precisely the kind of
# number this test may not print, and the labels are what say the report is complete.
/etc/vmstat -s | sed 's/^ *[0-9]* //' >>/tmp/inspect.log

kill $slept
echo ---end--- >>/tmp/inspect.log
sync
echo inspect done >/dev/console
