#!/bin/sh
# Boot the kernel and put all fifteen of task C5's text filters through it -- the booted pass
# task C5a deliberately did not take, closed here with C5b's seven beside its six.
#
# Invoked by ctest as: run-filters.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THE SAME SHAPE AS run-utils.sh, and for the same reasons: a script rather than a chain of
# ctest fixtures, because each stage's failure means something different and `set -e' names
# the one that stopped the run; its own copy of the image, grafted with `-a' rather than a
# line in ../../root.manifest, at its own SIMH volume (3097 -- see filters.ini for why not the
# 3096 cmd/TODO.md named) so that nothing here writes a build artifact and the tests may run
# in parallel.
#
# NOTHING IS MASKED, WHICH IS THE DIFFERENCE FROM run-utils.sh.  That script masks date's
# seconds field and time's three intervals, because those two programs print what the clock
# and the host decided.  Every number in this log is computed by a filter from a fixture
# filters.sh writes for itself in the same run -- counts, checksums, offsets, octal words --
# so the whole of it is reproducible and the diff is over every column.  If a mask ever
# becomes necessary here, the rule run-utils.sh states applies: mask the minimum, say which
# columns are dropped, and name the place the masked property IS asserted.
#
# WHAT SEPARATES THIS TEST FROM ITS b6sim HALF is filters.sh's header, at length: look(1)'s
# default dictionary, tail(1) on a pipe, col(1)'s argv[0], a grep(1) pattern with a space in
# it, the eight pipelines, and that the
# image's copy of C5a's six executes at all.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf filters.img root3097.disk filtersafter.img filters.out filters.log \
       filters0.drum filters1.drum
cp root.img filters.img
"$b6fsutil" -a filters.img /etc/filters "$srcdir/filters.sh"

"$b6fsutil" -S --volume=3097 filters.img root3097.disk

"$besm6" "$srcdir/filters.ini"

# What reached the disk.  Not this test's main question -- the log is -- but it is nearly free
# and the script does create eleven files in /tmp between the four here-documents, uniq's
# named output, tee's two, split's three and col's two, so a five-pass check is what says the
# allocation that took is sound.
"$b6fsutil" -S root3097.disk filtersafter.img
"$b6fsutil" -c filtersafter.img

mkdir filters.out
"$b6fsutil" -x filtersafter.img filters.out

diff -u "$srcdir/filters.expected" filters.out/tmp/filters.log
