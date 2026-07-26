#!/bin/sh
# Boot the kernel and let the shell run the twenty-three lib/test programs off the image,
# then bring the disk back to the host so that each program's output can be diffed.
# Task 25c: the same programs b6sim runs, run by the real kernel, held to the same files.
#
# Invoked by ctest as: run-libtest.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THIS SCRIPT IS THE SETUP FIXTURE AND NOTHING ELSE.  It stops once libtest.out/ exists;
# the fsck and the twenty-three diffs are ctest cases of their own (libtest_fsck,
# libtest_<name>), so a single program's disagreement names itself instead of being
# reported as "libtest failed".  That is the one way this differs from run-session.sh,
# where the sequence IS the test and there is one thing to check at the end.
#
# THE IMAGE THIS TEST USES IS ITS OWN.  root.img and root3072.disk are build artifacts that
# nothing writes; this makes a copy, grafts the driver script into it as /etc/libtest, and
# converts that to a SIMH container at volume 3080 (3072 is the artifact, mdtest owns
# 3073-3076, session 3077, console 3078, 3079 is the documented coninit scratch).  The
# volume number is the rightmost run of digits in the filename -- `attach' scrapes it, so
# the name IS the volume.
#
# ONLY THE DRIVER IS GRAFTED.  The programs themselves are on the shipped image, in
# /usr/test, put there by ../../root.manifest out of build/rootfs/ -- unlike session.sh,
# which is a fixture through and through.  They are the same linked images b6sim runs
# (lib/test/CMakeLists.txt stages a copy, not a second link), so a difference between the
# two runs can only be the harness.
#
# `-S' TAKES ITS DIRECTION FROM THE INPUT -- it looks for the SIMH zone mark and converts
# the other way if it finds one -- so the same option builds the container before the run
# and unpacks it after.  fsck (`-c') and extract (`-x') work on the flat image only.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf libtest.img root3080.disk libafter.img libtest.out
cp root.img libtest.img
"$b6fsutil" -a libtest.img /etc/libtest "$srcdir/libtest.sh"
"$b6fsutil" -S --volume=3080 libtest.img root3080.disk

"$besm6" "$srcdir/libtest.ini"

# What reached the disk.  libtest_fsck checks the structure and libtest_<name> the
# contents; both work on what this leaves behind.
"$b6fsutil" -S root3080.disk libafter.img
mkdir libtest.out
"$b6fsutil" -x libafter.img libtest.out
