#!/bin/sh
# Boot the kernel on a machine too small for the load, and check on the host what came back.
# Task 26's "done when": more processes than core, swapped out and in through the drum, two
# of them sharing one binary's text, and every image intact afterwards.
#
# Invoked by ctest as: run-swap.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img, ../unix and the GENERATED swap.ini
# already are.  The .ini is generated (genboot.cmake substitutes four kernel addresses), so
# unlike session.ini it is taken from the build directory and not from $srcdir.
#
# WHY A SCRIPT AND NOT A CHAIN OF FIXTURES, and why the same shape as run-session.sh: six
# things have to happen in order and each one's failure means something different.  `set -e'
# stops at the first, and the stage that stopped it is named by the command ctest prints.
#
# THE IMAGE IS ITS OWN, at volume 3081 (3072 is the artifact, mdtest owns 3073-3076, session
# 3077, console 3078, the coninit bisect 3079, libtest 3080).  The volume number is the
# rightmost run of digits in the filename -- `attach' scrapes it, so the name IS the volume.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf swap.img root3081.disk swapafter.img swap.out
cp root.img swap.img
"$b6fsutil" -a swap.img /etc/swap "$srcdir/swap.sh"
"$b6fsutil" -S --volume=3081 swap.img root3081.disk

"$besm6" ./swap.ini

# What reached the disk.  The fsck is the structural half -- a swapper that put a page in
# the wrong place would as likely corrupt the buffer cache as an image -- and the diffs are
# the semantic half.
"$b6fsutil" -S root3081.disk swapafter.img
"$b6fsutil" -c swapafter.img

mkdir swap.out
"$b6fsutil" -x swapafter.img swap.out

# THE THREE CONCURRENT COPIES ARE HELD TO THE SAME EXPECTATION FILE b6sim IS HELD TO, with
# one substitution: each instance prints its own digit out of its own data segment, and the
# recorded run printed 0.  Reusing lib/test/puret.expected rather than recording three new
# files is the point -- a freshly recorded expectation would have made a kernel that fed one
# process another's data segment look correct (kernel/TODO.md says this at length about
# libtest, and it is the same argument).
for i in 1 2 3; do
    sed "s/^id 0\$/id $i/" "$srcdir/../../lib/test/puret.expected" > "expect-p$i"
    diff -u "expect-p$i" "swap.out/tmp/p$i.out"
done

# /tmp/big is /bin/ls copied while all three were running and the swapper was busy, through
# an inode big enough to need its indirect block.  Comparing it with the /bin/ls out of the
# SAME extracted image is what makes a dropped or misplaced block impossible to miss.
cmp swap.out/bin/ls swap.out/tmp/big

diff -u "$srcdir/swap.expected" swap.out/tmp/swap.log
