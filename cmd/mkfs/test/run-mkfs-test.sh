#!/bin/sh
# The layout oracle for mkfs(1M), under b6sim.  Task C4c.  Invoked by ctest as:
#
#	run-mkfs-test.sh SIM B6FSUTIL PROG NBLOCKS
#
# and what it asserts is that the guest's mkfs and the host's `b6fsutil -n' write THE SAME
# IMAGE, byte for byte.  That is available because cmd/fsutil/create.cpp is what ../mkfs.c
# was transcribed from and because the fixture starts as zeros: b6fsutil zeroes the whole
# volume it creates, and mkfs writes only the i-list, the free-list chain blocks, the root's
# directory block and the superblock, so every byte neither of them wrote is zero in both.
#
# THE TIMESTAMP IS THE ONLY THING IN THE WAY, and it is six bytes at a known offset.  mkfs
# stamps time(2) -- here the build machine's clock -- into s_time and into the root inode's
# three times.  s_time is word 6 of block 0, the superblock, so it sits at byte 6*6 = 36 and is
# six bytes big-endian (doc/File_Magic.md: a BESM-6 word reads as one big-endian 48-bit
# number).  Read it back out of what the guest wrote and hand it to `b6fsutil -T', and the
# other four agree by construction.
#
# THE FIELD DIFF COMES FIRST, because `cmp' says only "differ at byte N" and that is useless
# as a diagnostic.  `b6fsutil -v' prints the geometry, the i-list extent and all three free
# counts, so a diff of the two reports names the field that moved before the byte compare
# says how far.
set -e
sim=$1
b6fsutil=$2
prog=$3
nblk=$4

rm -f layout.img want.img layout.out got.sb want.sb
cp blank.img layout.img

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables through,
# so an emptied environment is the only one that is the same on every machine.
env -i "$sim" "$prog" layout.img "$nblk" >layout.out 2>&1 || {
    echo "mkfs failed:" >&2
    cat layout.out >&2
    exit 1
}
cat layout.out

# s_time, out of the image the guest just wrote.  `head -1' guards the awk against od's
# trailing blank line, which would otherwise be parsed as the number 0.
t=$(od -An -tu1 -j 36 -N 6 layout.img | head -1 |
    awk '{ v = 0; for (i = 1; i <= NF; i++) v = v * 256 + $i; print v }')
if [ -z "$t" ] || [ "$t" -le 0 ]; then
    echo "could not read s_time out of layout.img (got '$t')" >&2
    exit 1
fi

"$b6fsutil" -n -s "$nblk" -T "$t" want.img

# 1.  It is a filesystem at all -- five passes, over one no host tool built.
"$b6fsutil" -c layout.img

# 2.  Every field of the superblock, named.  `Last update' is the one line that is a date
#     rather than a number, and it is already pinned by -T; dropping it keeps the diff
#     readable if the two ever disagree about formatting rather than about content.
"$b6fsutil" -v want.img   | grep -v '^Last update' >want.sb
"$b6fsutil" -v layout.img | grep -v '^Last update' >got.sb
diff -u want.sb got.sb

# 3.  ... and the other 600 kilobytes.
cmp want.img layout.img

echo "cmd_mkfs_layout: $nblk blocks, byte-identical to b6fsutil -n"
