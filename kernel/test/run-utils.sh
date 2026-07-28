#!/bin/sh
# Boot the kernel and let task C2's small utilities do the three things only a real kernel
# can show: move the clock, wait on a delivered alarm, and signal another process.
#
# Invoked by ctest as: run-utils.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THE SAME SHAPE AS run-files.sh, and for the same reasons: a script rather than a chain of
# ctest fixtures, because each stage's failure means something different and `set -e' names
# the one that stopped the run; its own copy of the image, grafted with `-a' rather than a
# line in ../../root.manifest, at its own SIMH volume (3083) so that nothing here writes a
# build artifact and the tests may run in parallel.
#
# WHAT SEPARATES THIS TEST FROM files.  files asks what reached the DISK; this one asks what
# the three programs PRINTED, because for date(1) that is the whole of the evidence -- the
# weekday in its output is computed from the seconds count it built out of the argument, so
# a day's error in the leap-year arithmetic shows up as `Tue' where `Wed' belongs.  The fsck
# is still run, for the reason given below.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf utils.img root3083.disk utilsafter.img utils.out utils.log
cp root.img utils.img
"$b6fsutil" -a utils.img /etc/utils "$srcdir/utils.sh"

"$b6fsutil" -S --volume=3083 utils.img root3083.disk

"$besm6" "$srcdir/utils.ini"

# What reached the disk.  The fsck is not this test's main question, but it is nearly free
# and it answers one thing nothing else does: date(1) moves the system clock BACKWARDS and
# then forwards again while the filesystem is mounted, and the sync at the end of utils.sh
# writes that time into the superblock and into every inode the run touched.  A five-pass
# check is what says the filesystem does not mind.
"$b6fsutil" -S root3083.disk utilsafter.img
"$b6fsutil" -c utilsafter.img

mkdir utils.out
"$b6fsutil" -x utilsafter.img utils.out

# THE SECONDS ARE MASKED, and nothing else is.
#
# date(1) prints the instant it has just set, but it re-reads the clock through time(2) to
# do it, and the guest tick can roll over between the stime(2) and the read.  That would
# make the seconds field a coin toss -- so it is replaced by `SS' here, and the weekday,
# month, day, hour, minute, zone and year are all diffed as they stand.  This is the same
# projection run-files.sh makes with awk over `b6fsutil -v -v': the host keeps only the
# columns that are reproducible, and says which those are.
#
# The times in utils.sh are chosen away from any minute or day boundary, so a one-second
# slip cannot reach a field this leaves alone.
sed -e 's/\([0-9][0-9]:[0-9][0-9]:\)[0-9][0-9]/\1SS/' utils.out/tmp/utils.log >utils.log
diff -u "$srcdir/utils.expected" utils.log
