#!/bin/sh
# Boot the kernel and let task C8's five inspection programs look at it -- the half of that
# task b6sim cannot have, because b6sim has no plurality to look at.
#
# Invoked by ctest as: run-inspect.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THE SAME SHAPE AS run-filters.sh, and for the same reasons: a script rather than a chain of
# ctest fixtures, because each stage's failure means something different and `set -e' names
# the one that stopped the run; its own copy of the image, grafted with `-a' rather than a
# line in ../../root.manifest, at its own SIMH volume (3101) so that nothing here writes a
# build artifact and the tests may run in parallel.
#
# TWO THINGS ARE MASKED AND NOTHING ELSE IS, which for a test whose whole subject is the
# machine's volatile state is the number to be proud of.  Everything else in the log is a
# VERDICT computed in the guest -- inspect.sh compares there and prints the answer -- so the
# host has almost nothing left to project away:
#
#   dmesg's FOUR NUMBERS.  `phys mem', `user mem' and `swap size' are functions of how much
#   core and drum the simulator was given, and `root size' of how big this image happens to
#   be; all four would change under a retune that has nothing to do with dmesg.  The digits
#   become N and the four labels, their order and their units are diffed as they stand --
#   which is the whole of what this section is about, those four lines being the evidence
#   that a REAL kernel printf reached msgbuf rather than a banner somebody put there.
#
#   pstat -s's THREE NUMBERS, for the same reason: swplo, nswap and the swap device are
#   conf.c's, and the line is here to show the mode reads them at all.
#
# The rule run-utils.sh states holds: mask the minimum, say which columns are dropped, and
# name the place the masked property IS asserted.  Both of these are asserted as literals
# under b6sim -- cmd/dmesg/test/banner.expected carries the whole ring, and
# cmd/pstat/test/swap.expected carries all three scalars -- which is the two-harness split
# working exactly as ../../cmd/README.md SS9 describes it.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf inspect.img root3101.disk inspectafter.img inspect.out inspect.log \
       inspect0.drum inspect1.drum
cp root.img inspect.img
"$b6fsutil" -a inspect.img /etc/inspect "$srcdir/inspect.sh"

"$b6fsutil" -S --volume=3101 inspect.img root3101.disk

"$besm6" "$srcdir/inspect.ini"

# What reached the disk.  Not this test's question -- the log is -- but it is nearly free,
# and the script does create a dozen files in /tmp, so a five-pass check says the allocation
# that took is sound.
"$b6fsutil" -S root3101.disk inspectafter.img
"$b6fsutil" -c inspectafter.img

mkdir inspect.out
"$b6fsutil" -x inspectafter.img inspect.out

sed -e 's/^\(.*= \)[0-9]* kbytes$/\1N kbytes/' \
    -e 's/^swapdev .*$/swapdev MASKED/' \
    inspect.out/tmp/inspect.log >inspect.masked

diff -u "$srcdir/inspect.expected" inspect.masked
