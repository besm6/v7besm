# /etc/files -- what task C1b's four programs do to a real filesystem.
#
# Grafted into a copy of the root image by run-files.sh with `b6fsutil -a', NOT listed in
# ../../root.manifest: a test fixture has no business on the filesystem this port ships.  Run
# as `sh /etc/files' by files.ini, because there is no `#!' on this system.
#
# THIS SCRIPT IS HALF THE TEST.  It writes a log the host diffs against files.expected -- but
# the half that only the host can do is `b6fsutil -c' over what reached the disk, and the
# reason this test exists rather than a console dialogue is mv(1): renaming a directory into a
# DIFFERENT parent is four calls (link, unlink, unlink target/.., link parent target/..), and
# getting them wrong leaves a link count that reads back perfectly until an fsck.  So the
# script deliberately LEAVES /tmp/f/sub2 BEHIND -- a directory that changed parents -- for the
# host-side check to reconcile, the same way session.sh leaves /tmp/d.
#
# WHAT MAY REACH THE LOG: names and diagnostics.  No sizes, no times, no i-numbers -- an
# expectation file may record only what is reproducible.  Task C1c's block at the foot needed
# the other half of that rule and run-files.sh now has it: A MODE AND AN OWNER ARE ASKED OF THE
# IMAGE, NOT OF THE GUEST, with `b6fsutil -v -v' against files.modes.  `ls -l' would answer
# both here and prints a date beside them, and no time on this machine is reproducible.
#
# STDOUT AND STDERR ARE NEVER MIXED IN ONE COMMAND.  stdout is fully buffered to a file and
# flushed at exit while stderr is unbuffered, so a command writing both would interleave them
# unpredictably.  Each diagnostic below is redirected on the stream its program actually uses:
# cp and mv report on stderr, ln and rm on stdout (which is v7's inconsistency, not this
# port's).

echo files begin >/tmp/files.log

mkdir /tmp/f
mkdir /tmp/g

# ---- cp: file to file, file into a directory, a refusal, and a multi-block copy.
cp /etc/motd /tmp/f/m1
cp /etc/motd /tmp/g
cp /tmp/f/m1 /tmp/f/m1 2>>/tmp/files.log
cp /bin/ls /tmp/f/big
ls /tmp/f >>/tmp/files.log
ls /tmp/g >>/tmp/files.log

# ---- ln: a second name, a link into a directory, and the directory refusal.
ln /tmp/f/m1 /tmp/f/m2
ln /tmp/f/m1 /tmp/g
ln /tmp/f /tmp/g/dirlink >>/tmp/files.log
ls /tmp/f >>/tmp/files.log
ls /tmp/g >>/tmp/files.log

# ---- mv: rename a file, then move one into a directory.
mv /tmp/f/m2 /tmp/f/m3
mv /tmp/f/m3 /tmp/g
ls /tmp/f >>/tmp/files.log
ls /tmp/g >>/tmp/files.log

# ---- mv of a DIRECTORY, both branches of mvdir().
# The first keeps its parent, so `..' still points where it should and a link and an unlink
# are the whole of it.  The second changes parent, which is the four-call re-parent.
mkdir /tmp/g/sub
mv /tmp/g/sub /tmp/g/sub2
mv /tmp/g/sub2 /tmp/f
ls /tmp/f >>/tmp/files.log
ls /tmp/g >>/tmp/files.log

# ...and the two refusals that guard it.  The first walks the target's parents up through
# `..' and meets the source; the second is refused for spelling `..' at all, and needs two
# DIFFERENT parents to reach the test at all -- with one parent mvdir takes its simple branch
# and never looks.
mv /tmp/f /tmp/f/sub2 2>>/tmp/files.log
mv /tmp/g/../g /tmp/f/g2 2>>/tmp/files.log

# ---- rm: a file, a -f on a name that is not there, the refusal of a directory, and then
# the recursive descent -- which is a fork and an execl of /bin/rmdir per directory, and the
# only reason rm itself needs no privilege.
rm /tmp/g/m1
rm -f /tmp/g/nosuch
rm /tmp/f >>/tmp/files.log
mkdir /tmp/r
mkdir /tmp/r/a
mkdir /tmp/r/a/b
cp /etc/motd /tmp/r/a/b/m
rm -r /tmp/r
ls /tmp >>/tmp/files.log
ls /tmp/f >>/tmp/files.log
ls /tmp/g >>/tmp/files.log

# ---- C1c: chmod, chown, chgrp and touch -- the four that change an INODE rather than a
# directory.  WHAT THEY DO CANNOT REACH THIS LOG.  A mode, a uid and a gid are `ls -l' columns
# and `ls -l' also prints a date, which is not reproducible here -- SIMH's 250 Hz timer is
# calibrated against the host's wall clock.  So this half writes names and diagnostics only,
# and run-files.sh reads the modes and the owners back off the finished image with
# `b6fsutil -v -v', against files.modes.  How little of that clock this script actually sees is
# measured at the foot of this block, where touch(1) has to be asserted against it.
#
# cp gives each copy the source's mode, and /etc/motd is 0644 -- that is where every mode
# below starts from.
mkdir /tmp/c
cp /etc/motd /tmp/c/a
cp /etc/motd /tmp/c/b
cp /etc/motd /tmp/c/d
cp /etc/motd /tmp/c/e

# ---- chmod: the absolute form, then the whole symbolic grammar, one shape per file.
chmod 0666 /tmp/c/a                # absolute
chmod g-w /tmp/c/a                 # who + op `-'                            -> 646
chmod u=rwx,g=rx,o= /tmp/c/b       # three comma-separated clauses, `=', and an empty
chmod u+s,g+s /tmp/c/b             # ...then SETID, which chown below must NOT clear -> 6750
chmod +x /tmp/c/d                  # who OMITTED: `a' less the umask, and CMASK is 0
chmod +t /tmp/c/d                  # ...and STICKY, which survives because this shell is
                                   # root's -- chmod() drops ISVTX for anyone else -> 1755
chmod 0700 /tmp/c/e
chmod o=u /tmp/c/e                 # a permission copied from another who: o gets u's -> 707
chmod 0644 /tmp/c/nosuch 2>>/tmp/files.log

# ---- chown and chgrp: by name out of /etc/passwd and /etc/group, and by number.  Both
# diagnose an unknown name on STDOUT (printf) and a failed call on STDERR (perror), which is
# v7's inconsistency and why no command below redirects both.
chown guest /tmp/c/a
chgrp bin /tmp/c/a
chown 4 /tmp/c/b
chgrp 2 /tmp/c/b
chown nosuchuser /tmp/c/d >>/tmp/files.log
chgrp nosuchgroup /tmp/c/d >>/tmp/files.log
chown root /tmp/c/nosuch 2>>/tmp/files.log

# ---- touch: create, refuse to create with -c, fail on a path that cannot be made, and
# leave alone what it dates.
touch /tmp/c/new
touch -c /tmp/c/nonew 2>>/tmp/files.log
touch /tmp/nosuchdir/x 2>>/tmp/files.log
touch /tmp/c/a
ls /tmp/c >>/tmp/files.log

# ...and now the part that says utime(2) actually MOVED a time, which is the whole of what
# touch is for and the one thing this machine can barely see.
#
# WHY IT TAKES TWO GRAFTED FIXTURES.  Over the whole of this run the guest clock advances
# about two seconds -- compare root.img's `Last update' with filesafter.img's, which is the
# measurement -- so seventy-odd programs execute inside two ticks of a one-second mtime.  No
# two commands in this script are a tick apart, `ls -t' between two files it made is a coin
# toss (cmd/ls/ls.c's compar() returns 0 on equal times, into a qsort that is not stable),
# and asking for one would be asking for a test that fails at random.  So run-files.sh grafts
# /etc/aprobe and /etc/zprobe dated 2033 -- `b6fsutil -T 2000000000 -a' -- a generation ahead
# of the 2001 stamp this image and everything the guest writes carries.  Touching one of them
# drags it back thirty-two years, and THAT is a gap no tick granularity can blur.
#
# The two listings are printed together on purpose: plain ls gives the alphabetical order and
# `ls -t' must give the opposite one.  If touch did nothing, the two lines would agree.
touch /etc/aprobe
ls /etc/aprobe /etc/zprobe >>/tmp/files.log
ls -t /etc/aprobe /etc/zprobe >>/tmp/files.log

# Last, as in session.sh: there is no update daemon here, so nothing else flushes the cache.
sync
echo files done >/dev/console
