#!/bin/sh
# mkfs(1M) on a scratch drive -- the host half of kernel/test/mkfs.  Task C4c.
#
# Invoked by ctest as:
#
#	run-mkfs.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-fsinfo.sh's shape, which is the one that keeps the CONSOLE as part of its
# oracle: copy the pristine image, graft the guest script onto the copy, convert it to a SIMH
# container at this test's own volume number, boot with a SECOND drive attached, and then ask
# the host the questions the guest cannot answer about itself.
#
# What is different from every other test here is that the interesting volume is not the one
# the machine booted from.  scratch3090.disk comes into existence when SIMH formats it during
# `attach -n' and holds a filesystem nothing on the host built.
#
# SIX ORACLES, AND THE THIRD IS THE TASK.
#
#   1.  THE SCRATCH PACK IS STILL ITSELF.  `b6fsutil -S' converting the container back is an
#       assertion in its own right -- from_simh() validates the magic mark on every zone and
#       the self-address on every half-zone, and both are things kernel/dev/md.c writes from
#       memory on a write exchange.  It also REPORTS the volume number out of zone 0, and
#       that number is checked here, because the service words are per CONTROLLER: on a
#       two-drive machine a written zone would carry whatever pack was last READ.  md.c keeps
#       the mark per DRIVE for that reason, and this grep is what holds it to it -- zone 0
#       track 0 is block 0, which is the superblock and is written on every sync.
#       cmd/mkfs/README.md SS2 is the account.
#
#   2.  IT IS A FILESYSTEM.  `b6fsutil -c' -- five passes over one that no host tool built.
#
#   3.  IT IS THE SAME FILESYSTEM b6fsutil WOULD HAVE BUILT, byte for byte.  cmd/fsutil's
#       create.cpp is what cmd/mkfs/mkfs.c was transcribed from, and the fixture starts as
#       zeros both here (`attach -n' formats every data word to zero) and there
#       (Image::create()), so every byte neither program wrote is zero in both.  The one
#       thing in the way is the timestamp, which mkfs takes from the guest clock and which is
#       six bytes at a known offset -- s_time is word 6 of block 0, so byte 36 -- so it is
#       read back out of the guest's own superblock and handed to `-T'.
#       THE FIELD DIFF COMES FIRST, because `cmp' says only "differ at byte N", which is no
#       diagnostic at all; `b6fsutil -v' names the field that moved.
#
#   4.  df AGREES WITH THE HOST'S OWN FREE-LIST WALK, on a filesystem made ninety seconds
#       earlier -- run-fsinfo.sh's oracle 2, applied to the scratch pack.  And the second df
#       line is the control: the ROOT's free count must equal the host's reading of the root
#       image as it came back, which is what says mkfs touched nothing on drive 0.
#
#   5.  THE ROUND TRIP THROUGH mdwrite().  /etc/motd out to block 2 of the raw device and
#       back, cmp'd against the original OUT OF THE SAME EXTRACTED IMAGE -- run-dd.sh's
#       pattern, and the reason there is no fixture here to keep in step with etc/motd.
#       Only the first 365 bytes: dd padded the record out to a whole block with conv=sync.
#
#   6.  The log, diffed against mkfs.expected: the record counts of each transfer, the three
#       diagnostics the bad sizes earn and the non-zero status after each, and a closed
#       listing of /tmp.  NOTHING IS MASKED -- every number in it is arithmetic this test
#       chose, not something the clock decided.
#
# Plus `b6fsutil -c' on the root image too, which every writing test here ends with.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

NBLK=2000       # what the guest asks mkfs for: one whole EC-5052
KBPB=3          # 1024-byte blocks per 3072-byte filesystem block; cmd/README.md SS4
SBTIME=36       # byte offset of s_time: block 0 (SUPERB) x 3072, plus word 6 x 6

rm -rf mkfs.img root3089.disk scratch3090.disk mkfsafter.img scratch.img want.img \
       mkfs.out mkfs0.drum mkfs1.drum mkfs.console mkfs.check mkfs.rootcheck \
       mkfs.back scratch.convert want.sb got.sb

cp root.img mkfs.img
# /etc/mkfstest and NOT /etc/mkfs: the latter is the program this test is about, and `-a'
# would write the script straight over it.  mkfs.sh's header says so too.
"$b6fsutil" -a mkfs.img /etc/mkfstest "$srcdir/mkfs.sh"

"$b6fsutil" -S --volume=3089 mkfs.img root3089.disk

# scratch3090.disk is NOT made here: `attach -n' in mkfs.ini creates and formats it, which is
# the state a pack off the shelf is in and the state this test wants to start from.  It is
# removed above so that a rerun does not inherit the last run's filesystem and pass without
# writing anything.

# The transcript is an oracle here, so it is kept -- but it is also where a failing run says
# what went wrong, so it is echoed either way.
if ! "$besm6" "$srcdir/mkfs.ini" >mkfs.console 2>&1; then
    cat mkfs.console >&2
    exit 1
fi
cat mkfs.console

#
# Oracle 1.
#
"$b6fsutil" -S scratch3090.disk scratch.img | tee scratch.convert
if ! grep -q 'volume 3090' scratch.convert; then
    echo "run-mkfs.sh: the scratch pack came back claiming to be some other volume." >&2
    echo "  kernel/dev/md.c stamps each DRIVE's own mark into a written zone's service" >&2
    echo "  words (mdvol[]); zone 0 track 0 -- block 0, the superblock -- is the only one" >&2
    echo "  b6fsutil reads the volume from.  Suspect mdvol[] not being primed or not being" >&2
    echo "  stamped: without it a written zone carries whatever pack the CONTROLLER last" >&2
    echo "  read.  cmd/mkfs/README.md SS2." >&2
    cat scratch.convert >&2
    exit 1
fi

#
# Oracle 2, and oracle 4's input: -v so that the run ends with the block accounting.
#
"$b6fsutil" -c -v scratch.img | tee mkfs.check

#
# Oracle 3.  `head -1' guards the awk against od's trailing blank line, which would
# otherwise be parsed as the number 0.
#
t=$(od -An -tu1 -j $SBTIME -N 6 scratch.img | head -1 |
    awk '{ v = 0; for (i = 1; i <= NF; i++) v = v * 256 + $i; print v }')
if [ -z "$t" ] || [ "$t" -le 0 ]; then
    echo "run-mkfs.sh: could not read s_time out of the scratch image (got '$t')" >&2
    exit 1
fi
"$b6fsutil" -n -s $NBLK -T "$t" want.img

"$b6fsutil" -v want.img    | grep -v '^Last update' >want.sb
"$b6fsutil" -v scratch.img | grep -v '^Last update' >got.sb
if ! diff -u want.sb got.sb; then
    echo "run-mkfs.sh: the guest's superblock and b6fsutil -n's disagree, above." >&2
    exit 1
fi
if ! cmp want.img scratch.img; then
    echo "run-mkfs.sh: the two superblocks agree field for field but the volumes differ." >&2
    echo "  Everything outside the i-list, the chain blocks, the root's directory block" >&2
    echo "  and block 1 should be zero in both; look there first." >&2
    exit 1
fi
echo "run-mkfs.sh: the guest's filesystem is byte-identical to b6fsutil -n -s $NBLK"

#
# ... and now the root image, which the rest of the checks want.
#
"$b6fsutil" -S root3089.disk mkfsafter.img
"$b6fsutil" -c -v mkfsafter.img | tee mkfs.rootcheck

mkdir mkfs.out
"$b6fsutil" -x mkfsafter.img mkfs.out

#
# Oracle 4.  Both df rows, each against its own device's free-list walk.  The two sides are
# in different units on purpose -- b6fsutil counts filesystem blocks, df reports 1024-byte
# ones -- and converting here, once, is run-fsinfo.sh's arrangement.
#
# A row is now a TABLE ROW: `Filesystem 1K-blocks Used Avail Capacity Mounted on', and Avail
# is field 4.  The freshly made /dev/rmd1 is mounted nowhere, so its Mounted on column is `-';
# the control /dev/rmd0 is the root, but it is named as the RAW device and /etc/mtab does not
# hold the root, so df finds `/' for it through the block twin /dev/md0.  Neither column is
# checked here -- kernel/test/mount is where the mount point is the assertion.
#
console=$(tr -d '\r' <mkfs.console | sed -n '/^---mkfs---$/,/^---endmkfs---$/p')
for dev in rmd1 rmd0; do
    case $dev in
    rmd1) check=mkfs.check ;;
    rmd0) check=mkfs.rootcheck ;;
    esac
    blocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' "$check")
    if [ -z "$blocks" ]; then
        echo "run-mkfs.sh: b6fsutil -c -v printed no block accounting -- see $check" >&2
        exit 1
    fi
    want=$((blocks * KBPB))
    # The Avail column, found by the row's first field -- which also skips df's header, whose
    # first field is `Filesystem', and mkfs's own summary, whose first field is `mkfs:'.
    got=$(echo "$console" | awk -v d="/dev/$dev" 'NF >= 5 && $1 == d { print $4 }')
    if [ -z "$got" ]; then
        echo "run-mkfs.sh: no df output for /dev/$dev between the markers" >&2
        echo "$console" >&2
        exit 1
    fi
    if [ "$got" != "$want" ]; then
        echo "run-mkfs.sh: df says /dev/$dev has $got free 1K-blocks, the host's free-list" \
             "walk says $want ($blocks filesystem blocks x $KBPB)" >&2
        exit 1
    fi
    echo "run-mkfs.sh: df and b6fsutil agree on $want free 1K-blocks for /dev/$dev"
done

# And mkfs's own summary line, against the same numbers the host has.  Nothing checked in:
# `b6fsutil -v' prints the volume size and the first data block, and the i-list extent is
# the two of them.
isize=$(sed -n 's/^First data block: *\([0-9]*\)$/\1/p' want.sb)
ninode=$(sed -n 's/^I-list: *blocks [0-9]*\.\.[0-9]* (\([0-9]*\) inodes)$/\1/p' want.sb)
ilist=$(sed -n 's/^I-list: *blocks \([0-9]*\)\.\..*$/\1/p' want.sb)
wantline="mkfs: /dev/rmd1: $NBLK blocks, $ninode inodes in blocks $ilist..$((isize - 1)), first data block $isize"
gotline=$(echo "$console" | grep '^mkfs: ')
if [ "$gotline" != "$wantline" ]; then
    echo "run-mkfs.sh: mkfs's summary line and the host's reading of the image disagree" >&2
    echo "  guest: $gotline" >&2
    echo "  host:  $wantline" >&2
    exit 1
fi
echo "run-mkfs.sh: mkfs's summary line agrees with the finished image"

#
# Oracle 5.  conv=sync padded /etc/motd out to a whole block, so only its own length counts.
#
motd=$(wc -c <mkfs.out/etc/motd | tr -d ' ')
dd if=mkfs.out/tmp/back bs=1 count="$motd" of=mkfs.back 2>/dev/null
if ! cmp mkfs.out/etc/motd mkfs.back; then
    echo "run-mkfs.sh: what came back off block 2 of /dev/rmd1 is not what went out." >&2
    echo "  This is the first raw WRITE this system does; suspect mdwrite() and the" >&2
    echo "  drainbrz() in mdstart(), which is what puts the buffer in memory the" >&2
    echo "  controller can see." >&2
    exit 1
fi
echo "run-mkfs.sh: $motd bytes went out through mdwrite() and came back unchanged"

#
# Oracle 6.
#
diff -u "$srcdir/mkfs.expected" mkfs.out/tmp/mkfs.log
