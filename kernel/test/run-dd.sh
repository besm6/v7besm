#!/bin/sh
# dd(1) on the raw device -- the host half of kernel/test/dd.  Task C4b.
#
# Invoked by ctest as:
#
#	run-dd.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-fsinfo.sh's shape: copy the pristine image, graft the guest script onto the
# copy, convert it to a SIMH container at this test's own volume number, boot, convert what
# comes back, fsck it, extract it, and ask the host the questions the guest cannot answer
# about itself.
#
# FOUR ORACLES, AND THE FIRST TWO ARE THE TASK.
#
#   1.  THE RAW READ, AGAINST THE IMAGE ITSELF.  The guest copied two blocks off /dev/rmd0
#       and this compares them, byte for byte, with the same two blocks of the container it
#       was handed -- and it does it twice, because the guest asked twice: once with no bs= at
#       all, which is the default record size this port changed from 512 to BSIZE and which
#       goes through the per-byte loop, and once with bs=3072, which takes the fast path.
#       Nothing checked in, nothing recomputed: the oracle is the disk.
#
#   2.  A GUARD ON THAT ORACLE, and it exists because oracle 1 compares against the image as
#       it was BEFORE the boot.  That is the state the guest read -- the raw read is the first
#       thing dd.sh does -- but it is only trustworthy while nothing in the run rewrites those
#       blocks.  So the after-image is checked at the same offset, and if it has moved, this
#       says which block to move rather than leaving oracle 1 to fail as a mystery.
#
#   3.  THE EBCDIC ROUND TRIP.  /etc/motd through atoe[] and back through etoa[], cmp'd
#       against the original OUT OF THE SAME EXTRACTED IMAGE -- run-edit.sh's pattern, and the
#       reason there is no fixture here to keep in step with etc/motd.  It is 365 bytes of
#       UTF-8, so what it asserts is that the two tables are inverses over all 256 byte
#       values and not merely over the ASCII half.
#
#   4.  The log, diffed against dd.expected: the record counts of each transfer, the two
#       diagnostics the misaligned records earn and the non-zero status that follows each,
#       ls -s on what was read, and a closed listing of /tmp.  NOTHING IS MASKED -- every
#       number in it is arithmetic on sizes this test chose, not something the clock decided.
#
# Plus `b6fsutil -c', which every writing test here ends with.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

# The blocks the guest read.  dd.sh's header says why 100 and not something more interesting:
# this image's data area runs from block 34 to about 530 and holds program text nobody
# rewrites, while everything the run allocates comes off the free list well above it.
SKIP=100
COUNT=2
BS=3072

rm -rf dd.img root3088.disk ddafter.img dd.out dd0.drum dd1.drum \
       dd.console dd.want dd.after
cp root.img dd.img
"$b6fsutil" -a dd.img /etc/dd "$srcdir/dd.sh"

"$b6fsutil" -S --volume=3088 dd.img root3088.disk

# Not an oracle here -- unlike fsinfo, nothing this test asserts is on the console -- but it
# is where a failing run says what went wrong, so it is echoed either way.
if ! "$besm6" "$srcdir/dd.ini" >dd.console 2>&1; then
    cat dd.console >&2
    exit 1
fi
cat dd.console

"$b6fsutil" -S root3088.disk ddafter.img
"$b6fsutil" -c ddafter.img

mkdir dd.out
"$b6fsutil" -x ddafter.img dd.out

#
# Oracle 1's expectation, taken off the flat image with the host's own dd: block N of the
# volume is byte N*BSIZE of the image, which is the same arithmetic cmd/dd did through
# lseek(2) on the device.
#
dd if=dd.img of=dd.want bs=$BS skip=$SKIP count=$COUNT 2>/dev/null

# Oracle 2 first, because it is the one that explains a failure of oracle 1.
dd if=ddafter.img of=dd.after bs=$BS skip=$SKIP count=$COUNT 2>/dev/null
if ! cmp -s dd.want dd.after; then
    echo "run-dd.sh: the run rewrote block $SKIP, so the raw read cannot be checked against" \
         "the image it was handed.  Pick a block this run does not touch -- the data area is" \
         "34 to about 530 and everything allocated here comes off the free list above it --" \
         "and change SKIP in this file and in dd.sh together." >&2
    exit 1
fi

# Oracle 1.  Both paths through dd, against the same expectation.
for f in blkA blkB; do
    if ! cmp dd.want dd.out/tmp/$f; then
        echo "run-dd.sh: /tmp/$f does not match blocks $SKIP..$((SKIP + COUNT - 1)) of the" \
             "image.  dd read the wrong place or read it wrongly; the seek is the first" \
             "thing to suspect, being the one of the four conditions in cmd/df/README.md" \
             "that physio() does not diagnose." >&2
        exit 1
    fi
done
echo "run-dd.sh: both raw reads match blocks $SKIP..$((SKIP + COUNT - 1)) of the image," \
     "$((COUNT * BS)) bytes each"

# Oracle 3.
cmp dd.out/etc/motd dd.out/tmp/motd.a
echo "run-dd.sh: /etc/motd survived ASCII -> EBCDIC -> ASCII intact," \
     "$(wc -c <dd.out/etc/motd | tr -d ' ') bytes"

# Oracle 4.
diff -u "$srcdir/dd.expected" dd.out/tmp/dd.log
