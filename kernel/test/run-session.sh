#!/bin/sh
# Boot the kernel with a WRITABLE root filesystem, let a shell script write to it, and then
# check on the host what actually reached the disk.  Task 25b's "done when": the image
# survives boot -> shell -> create and write files -> sync -> fsck clean.
#
# Invoked by ctest as: run-session.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# WHY THIS IS A SCRIPT AND NOT A CHAIN OF CTEST FIXTURES.  Five things have to happen in
# order and each one's failure means something different; a fixture chain would report them
# all as "session failed".  `set -e' makes the first non-zero exit stop the run, and the
# stage that stopped it is named by the command ctest prints.
#
# THE IMAGE THIS TEST USES IS ITS OWN.  root.img and root3072.disk are build artifacts that
# nothing writes; this makes a copy, grafts the session script into it as /etc/session, and
# converts that to a SIMH container at volume 3077 (3072 is the artifact, mdtest owns
# 3073-3076, console owns 3078).  The volume number is the rightmost run of digits in the
# filename -- `attach' scrapes it, so the name IS the volume.
#
# `-a' rather than a line in ../../root.manifest is what keeps a test fixture off the root
# filesystem the kernel really ships.  The same graft is how coninit can be put back as
# /etc/init by hand; kernel/test/CMakeLists.txt spells that one out.
#
# `-S' TAKES ITS DIRECTION FROM THE INPUT -- it looks for the SIMH zone mark and converts the
# other way if it finds one -- so the same option builds the container before the run and
# unpacks it after.  fsck (`-c') and extract (`-x') work on the flat image only.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf session.img root3077.disk after.img session.out
cp root.img session.img
"$b6fsutil" -a session.img /etc/session "$srcdir/session.sh"
"$b6fsutil" -S --volume=3077 session.img root3077.disk

"$besm6" "$srcdir/session.ini"

# What reached the disk.  The fsck is the structural half -- five passes over the inodes,
# the free list and the directory tree, exit 1 on any problem -- and the diff is the
# semantic half: the file the session built, byte for byte.
"$b6fsutil" -S root3077.disk after.img
"$b6fsutil" -c after.img

mkdir session.out
"$b6fsutil" -x after.img session.out
diff -u "$srcdir/session.expected" session.out/tmp/session.log

# /tmp/big is /bin/ls copied by cat(1) through an inode big enough to need its indirect
# block.  Comparing it with the /bin/ls out of the SAME extracted image is what makes a
# dropped, duplicated or misplaced block impossible to miss -- and it needs no checked-in
# copy of a binary to compare against.
cmp session.out/bin/ls session.out/tmp/big
