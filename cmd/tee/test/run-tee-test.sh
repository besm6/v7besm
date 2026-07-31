#!/bin/sh
# The FILE oracle for tee(1), under b6sim.  Task C5a.  Invoked by ctest as:
#
#	run-tee-test.sh SIM PROG SRCDIR
#
# WHY THIS IS NOT A b6_progtest CASE, which is the reusable half of it.  b6_progtest() diffs
# standard output and checks an exit status and can do no more (../../../scripts/
# run-prog-test.sh) -- and tee's whole job is the files it creates, which standard output says
# nothing about.  A program whose output is a FILE needs its assertion out of band: run the
# guest, then let the host look at what is on the disk.  ../../mkfs/test/run-mkfs-test.sh is
# the shape this copies; the difference is that mkfs's oracle is a second implementation and
# this one is simply the input, which is all tee promises.
#
# Every later filter that writes a file rather than a stream -- split here, and col, `sed -n
# w' and `sort -o' when they come -- takes the same shape.
#
# THE APPEND HALF IS THE PART A CARELESS TEST WOULD MISS.  `tee -a' opens for writing and
# seeks to the end, and if the seek were dropped the second run would overwrite rather than
# append and the file would still compare equal to the input.  So the assertion is that the
# file is the input TWICE, which only an append can produce.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf teedir
mkdir teedir
cp "$srcdir/data.txt" teedir/in
cd teedir

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables through,
# so an emptied environment is the only one that is the same on every machine.

# 1.  Two outputs and standard output, all three carrying the same bytes.
env -i "$sim" "$prog" out1 out2 <in >stdout.txt 2>&1
cmp in stdout.txt
cmp in out1
cmp in out2

# 2.  -a appends rather than truncating, so each file is now the input twice.
env -i "$sim" "$prog" -a out1 out2 <in >stdout2.txt 2>&1
cmp in stdout2.txt
cat in in >twice.txt
cmp twice.txt out1
cmp twice.txt out2

# 3.  ... and without -a it truncates back, which is the other half of the same switch.
env -i "$sim" "$prog" out1 <in >stdout3.txt 2>&1
cmp in out1

echo "cmd_tee_files: stdout and both files carry the input, and -a appends"
