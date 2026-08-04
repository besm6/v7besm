# /etc/libtest -- the script kernel/test/libtest drives, run as sh /etc/libtest.
#
# THIS FILE IS NOT IN root.manifest and is not part of the shipped root filesystem.
# run-libtest.sh grafts it onto its own converted copy of the image with b6fsutil -a,
# which is what keeps a test fixture out of the tree the kernel really boots.  The
# PROGRAMS it runs are on the shipped image, in /usr/test -- only the driver is grafted.
#
# The hash is a real comment: this shell has one, which v7's had not -- see
# cmd/sh/README.md.  These lines used to begin with `:', the null built-in, whose words
# were still parsed.
#
# WHAT THIS PROVES.  Twenty-three of the programs of lib/test also run under b6sim, where
# every system call is served by the HOST -- so a bug in this kernel cannot show there.
# Here they run on the real thing, and the same .expected files adjudicate.  This is the
# first time the extracode gate is driven from user mode by anything but the kernel own
# tests and the eight commands of /bin, and it is where sysent rows meet a real caller.
#
# FIVE OF THE TWENTY-EIGHT ARE HERE ONLY.  shellt needs a /bin/sh that really starts; memt
# reads the kernel own memory through /dev/kmem and its own image through /dev/mem; suidt
# drops to uid 7 and execs /bin/mkdir, which no host has; curstty needs a console whose
# tty modes are real; and ttyt needs the /dev the host will not let it read and the /etc/ttys
# the host has none of.  See lib/test/progs.cmake.
#
# FOUR THINGS BELOW ARE LOAD-BEARING, and each matches lib/test/run-test.sh so that the
# expectation files transfer unchanged:
#
#   cd /usr/test, and every program spelled ./name.  hello prints its own argv zero and
#       the expectation contains the literal dot-slash-hello.  execs execs ITSELF through
#       argv zero five times, and its execvp stage needs a name with a slash in it so that
#       no path search happens.
#
#   the redirection of both descriptors to a file.  gen asserts that isatty of descriptor
#       1 is false, which is true only because of it, and perror writes to descriptor 2.
#
#   an exported variable.  environ asserts the environment is not empty, and that a proper
#       prefix of the first entry name does not match -- a check it only makes when that
#       name is longer than one character.  b6sim hands the guest a whitelist of the host
#       variables.  Here the shell hands over what it was told to export, and nothing else,
#       so without the two lines below the test would pass by being vacuous.  It is weaker
#       here than under b6sim either way.
#
#   sync, LAST.  There is no update daemon on this system, so without it the delayed-write
#       buffers holding every one of those output files would still be in core when the
#       simulator stops.
#
# EACH PROGRAM ANNOUNCES ITSELF ON THE CONSOLE when it is done.  That is not decoration:
# libtest.ini has one expect rule per line, and a rule that fires restarts the machine, so
# the run is bounded by the ctest timeout rather than by one step budget -- and a program
# that hangs is named by the last marker that appeared.  The programs own output goes to
# files, so nothing else is in that stream to match by accident.

cd /usr/test

LIBTEST=besm6
export LIBTEST

./hello one two three >/tmp/hello.out 2>&1
echo ok hello >/dev/console

./vararg >/tmp/vararg.out 2>&1
echo ok vararg >/dev/console

./errno >/tmp/errno.out 2>&1
echo ok errno >/dev/console

./procs >/tmp/procs.out 2>&1
echo ok procs >/dev/console

./sbrkt >/tmp/sbrkt.out 2>&1
echo ok sbrkt >/dev/console

./malloct >/tmp/malloct.out 2>&1
echo ok malloct >/dev/console

./strings >/tmp/strings.out 2>&1
echo ok strings >/dev/console

./gen >/tmp/gen.out 2>&1
echo ok gen >/dev/console

./strtolt >/tmp/strtolt.out 2>&1
echo ok strtolt >/dev/console

./environ >/tmp/environ.out 2>&1
echo ok environ >/dev/console

./jmp >/tmp/jmp.out 2>&1
echo ok jmp >/dev/console

./headers >/tmp/headers.out 2>&1
echo ok headers >/dev/console

./stdiot >/tmp/stdiot.out 2>&1
echo ok stdiot >/dev/console

./printft >/tmp/printft.out 2>&1
echo ok printft >/dev/console

./scanft >/tmp/scanft.out 2>&1
echo ok scanft >/dev/console

./execs >/tmp/execs.out 2>&1
echo ok execs >/dev/console

./shellt >/tmp/shellt.out 2>&1
echo ok shellt >/dev/console

./memt >/tmp/memt.out 2>&1
echo ok memt >/dev/console

./kctlt >/tmp/kctlt.out 2>&1
echo ok kctlt >/dev/console

./suidt >/tmp/suidt.out 2>&1
echo ok suidt >/dev/console

./timet >/tmp/timet.out 2>&1
echo ok timet >/dev/console

./pwent >/tmp/pwent.out 2>&1
echo ok pwent >/dev/console

# NOT redirected the same way by accident: descriptor 0 stays the shell's terminal here as it
# does for every program above, and that is the whole premise of ttyt -- ttyname(0) has an
# answer only because of it.
./ttyt >/tmp/ttyt.out 2>&1
echo ok ttyt >/dev/console

# Directories, and it is here ONLY for the same reason ttyname() is: b6sim's read(2) is the
# host's and refuses a directory descriptor.  What makes this one worth saying twice is that
# the failure there does not look like a failure -- the host's open() and fstat() both
# SUCCEED, so opendir() hands back a good DIR and every directory reads as empty.
./dirt >/tmp/dirt.out 2>&1
echo ok dirt >/dev/console

./signals >/tmp/signals.out 2>&1
echo ok signals >/dev/console

./matht >/tmp/matht.out 2>&1
echo ok matht >/dev/console

# termcapt is not a libc test: it is lib/libtermcap's, and the argument is load-bearing.
# /etc/termcap is the database it reads, and it must be named because tgetent treats a
# relative TERMCAP as an entry rather than a file name.  Under b6sim the same program is
# handed the SAME FILE out of the source tree, which is what lets one expectation file
# adjudicate both runs -- see lib/test/termcapt.args and the source's own header.

./termcapt /etc/termcap >/tmp/termcapt.out 2>&1
echo ok termcapt >/dev/console

# cursest belongs to lib/libcurses, and its argument is /etc/termcap for the same reason
# termcapt takes one.  It sets My_term itself, so initscr never calls gettmode and never
# asks this console what its modes are -- which is what lets one expectation adjudicate
# this run and the b6sim one, since b6sim answers every ioctl with success and does
# nothing while here the console is really ECHO plus CRMOD plus XTABS.  Those flags
# change which cursor motion curses emits.

./cursest /etc/termcap >/tmp/cursest.out 2>&1
echo ok cursest >/dev/console

# puret is the PURE image -- linked NMAGIC -- and the only one on the disk whose text the
# kernel shares rather than copies.  It runs last but one because it is the newest, and
# because a failure here means the exec path rather than a library routine.

./puret >/tmp/puret.out 2>&1
echo ok puret >/dev/console

# curstty is the other half of cursest, and it runs HERE ONLY: it is the tty modes cursest
# has to switch off -- gettmode, savetty, resetty, raw, cbreak, echo and nl on a real
# terminal -- and what it asserts is the console this kernel opens in sc.c.
#
# IT RUNS LAST ON PURPOSE.  gettmode clears XTABS on the console and only endwin puts it
# back, so if it ever dies the only thing left running against a changed console is one
# echo and the sync below.

./curstty /etc/termcap >/tmp/curstty.out 2>&1
echo ok curstty >/dev/console

sync
echo libtest done >/dev/console
