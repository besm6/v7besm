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
# expectation file may record only what is reproducible.
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

# Last, as in session.sh: there is no update daemon here, so nothing else flushes the cache.
sync
echo files done >/dev/console
