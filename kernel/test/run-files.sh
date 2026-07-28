#!/bin/sh
# Boot the kernel with a WRITABLE root filesystem, let cp, ln, mv and rm rearrange it, and
# then check on the host what actually reached the disk.  Task C1b's "done when".
#
# Invoked by ctest as: run-files.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THE SAME SHAPE AS run-session.sh, and for the same reasons: a script rather than a chain of
# ctest fixtures, because each stage's failure means something different and `set -e' names
# the one that stopped the run; its own copy of the image, grafted with `-a' rather than a
# line in ../../root.manifest, at its own SIMH volume (3082) so that nothing here writes a
# build artifact and the tests may run in parallel.
#
# WHAT SEPARATES THIS TEST FROM session.  session proves that a shell can CREATE files.  This
# one proves that the file-management set can rearrange them -- and the reason it needs the
# fsck rather than a console dialogue is one program: mv(1) renaming a directory into a
# different parent is four separate calls, and a link count it gets wrong reads back perfectly
# from the running kernel.  Only a pass over the whole i-list can see it.  files.sh therefore
# leaves a re-parented directory on the disk on purpose.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf files.img root3082.disk filesafter.img files.out
cp root.img files.img
"$b6fsutil" -a files.img /etc/files "$srcdir/files.sh"

# Two more fixtures, and their CONTENT is irrelevant -- their DATE is the whole point.  `-T
# 2000000000' stamps them 2033, a generation ahead of the 1000000000 this image and everything
# the guest writes carries, and files.sh touches one of the two.  That is the only way this
# machine can see touch(1) move a time at all: the guest clock advances about two seconds over
# the whole run, so no two commands in the script are one mtime tick apart.  files.sh says it
# at length.
"$b6fsutil" -T 2000000000 -a files.img /etc/aprobe "$srcdir/files.sh"
"$b6fsutil" -T 2000000000 -a files.img /etc/zprobe "$srcdir/files.sh"

"$b6fsutil" -S --volume=3082 files.img root3082.disk

"$besm6" "$srcdir/files.ini"

# What reached the disk.  The fsck is the half that matters here -- five passes over the
# inodes, the free list and the directory tree, exit 1 on any problem, and the only thing that
# can judge what mv did to /tmp/f/sub2's `..' and to the two parents' link counts.  The diff
# is the semantic half: what the programs said and what the directories then held.
"$b6fsutil" -S root3082.disk filesafter.img
"$b6fsutil" -c filesafter.img

mkdir files.out
"$b6fsutil" -x filesafter.img files.out
diff -u "$srcdir/files.expected" files.out/tmp/files.log

# /tmp/f/big is /bin/ls copied by cp(1), through an inode big enough to need its indirect
# block (NADDR is 6 direct blocks of 3072 bytes).  Comparing it with the /bin/ls out of the
# SAME extracted image is what makes a dropped, duplicated or misplaced block impossible to
# miss -- and it is cp's read/write loop that is on trial, crossing its BUFSIZ boundary five
# times rather than session.sh's cat.
cmp files.out/bin/ls files.out/tmp/f/big

# ---- Task C1c: what chmod, chown and chgrp actually wrote into the inodes.
#
# THIS IS THE HALF OF C1c THAT CANNOT BE ASKED IN THE GUEST.  A mode and an owner are `ls -l'
# columns, and `ls -l' also prints a date -- the superblock's s_time is pinned by the image's
# -T stamp, but SIMH's 250 Hz tick is calibrated against the HOST's clock, so no time on this
# machine is reproducible.  files.sh's standing rule follows from that: only names and
# diagnostics may reach the log.  So the question is put to the image instead.
#
# `b6fsutil -v -v' prints one line per path -- type, mode in octal, uid/gid, size, i-number
# and link count.  The awk keeps the first three fields and the LAST one: the size is not what
# is on trial, the i-number moves whenever anything earlier in the script changes, and the link
# count is the fsck's business four lines up.  Sorted on the path so that the expectation does
# not encode the order in which the walk happens to meet the directory entries.
#
# A DIFF AND NOT A CHAIN OF greps, unlike rootimg_setuid (../test/CMakeLists.txt): that one
# asserts three facts about the pristine image and can afford to be open.  This one asserts
# fifteen and has to be CLOSED -- the whole of /tmp is listed, C1b's leftovers included, so
# that a mode changed which no line of files.sh asked to change fails the run too.
"$b6fsutil" -v -v filesafter.img |
    awk '$NF ~ /^\/tmp(\/|$)/ { print $1, $2, $3, $NF }' |
    LC_ALL=C sort -k4,4 >files.modes
diff -u "$srcdir/files.modes" files.modes

# ...and touch(1) did not disturb a byte of what it dated.  /tmp/c/a is /etc/motd copied,
# chmod'ed, chown'ed, chgrp'ed and finally touched; utime(2) never opens the file, and this is
# what says so.
cmp files.out/etc/motd files.out/tmp/c/a
