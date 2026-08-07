#!/bin/sh
# Boot the kernel and have it run one user program, /usr/test/coret -- the check on
# kernel/TODO.md's `int'-vs-pointer sweep.  core.ini is the dialogue and its header is the
# account of it.
#
# Invoked by ctest as: run-core.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD directory
# as the working directory -- where root.img and ../unix already are.
#
# THIS SCRIPT ASSERTS ALMOST NOTHING, and that is deliberate: coret adjudicates itself, on the
# console, and core.ini fires on the first `FAIL' line it prints.  A program that can read its
# own core file back can also say whether it read the right thing, and saying it there keeps the
# expectation next to the code that knows what it means -- rather than in a .expected file on the
# host that has to be re-derived every time the image's layout moves.
#
# What is left for the host is the one thing the guest cannot see: that the filesystem it wrote
# is still consistent afterwards.  coret creates and removes a core file in /tmp -- an allocate
# and a free through alloc.c and free.c, with the superblock totals maintained on both sides --
# so a `COUNT WRONG IN SUPERBLK' here would mean that path drifted.  It costs a second.
#
# THE DISK IS A COPY, at volume 3086: root3072.disk is the build artifact and nothing writes it.
# `-S' takes its direction from the input -- it looks for the SIMH zone mark and converts the
# other way if it finds one -- so the same option builds the container before the run and unpacks
# it after.
#
# THE DRUMS ARE `-n' AND ARE NAMED FOR THIS TEST.  ctest runs the SIMH tests in parallel; two
# tests sharing a swap container would swap over each other.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -f core.img root3086.disk coreafter.img core0.drum core1.drum
cp root.img core.img
"$b6fsutil" -S --volume=3086 core.img root3086.disk

"$besm6" "$srcdir/core.ini"

"$b6fsutil" -S root3086.disk coreafter.img
"$b6fsutil" -c coreafter.img
