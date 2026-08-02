#!/bin/sh
# tar(1) on a running system -- the host half of kernel/test/tar.  Task C7.
#
# Invoked by ctest as:
#
#	run-tar.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-mkfs.sh's shape: copy the pristine image, graft the guest script onto the
# copy, convert it to a SIMH container at this test's own volume number, boot with a SECOND
# drive attached, and then ask the host the questions the guest cannot answer about itself.
#
# FIVE ORACLES, AND THE THIRD IS THE ONE THAT COULD NOT BE HAD ANYWHERE ELSE.
#
#   1.  The log, diffed against tar.expected.  Nothing is masked: every line in it is a name,
#       a diagnostic or an exit status that tar, cmp, find or sort computed from a corpus
#       tar.sh writes in the same run.  Two of its sections are a matched pair -- ---mtime---
#       must be EMPTY and ---mtime-m--- must list all six files -- which is what makes the
#       mtime assertion a test rather than a tautology.
#
#   2.  THE MODES, OWNERS AND HARD LINK, off the finished image rather than out of the guest.
#       C1's rule: `ls -l' in the guest prints a date nothing can reproduce, so what an
#       extraction restored is read with `b6fsutil -v -v', which prints type, mode, uid/gid,
#       size, i-number and nlink for every path on the volume.  The hard link is the sharp
#       one -- /tmp/back/tree/one and hardlink must come back as ONE inode with nlink 2,
#       which is the whole of what the header's linkflag '1' means -- and it is asserted on
#       the EXTRACTED copies, not on the corpus, because the corpus's link was made by ln(1)
#       and proves nothing about tar.
#
#   3.  THE HOST'S OWN tar READS THE ARCHIVE STRAIGHT OFF THE DISK PACK.  Section 9 of the
#       guest script writes onto the bare /dev/rmd1 -- no filesystem, no mkfs, just records
#       at a blocking factor physio() will accept -- so the converted container IS a tar file
#       with the drive's zeros behind it, and tar stops at the first zero header.  If the
#       alignment, the blocking factor or the write path were wrong in any way the bytes
#       would not parse at all.  ../../cmd/TODO.md said `tar cf /dev/rmd0' worked already;
#       this is what it would have taken to know.
#
#   4.  The scratch pack is still itself.  `b6fsutil -S' converting it back validates the
#       magic mark on every zone and the self-address on every half-zone, both written from
#       memory by kernel/dev/md.c on a write exchange -- run-mkfs.sh's oracle 1, and it
#       applies here for the same reason: the service words are per CONTROLLER and md.c has
#       to keep the label per DRIVE.
#
#   5.  `b6fsutil -c' on the root image, which every writing test here ends with.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf tar.img root3099.disk scratch3100.disk tarafter.img scratch-tar.img \
       tar.out tar0.drum tar1.drum tar.console tar.check tar.rootcheck \
       tar.convert tar.listing tar.rawnames

cp root.img tar.img
# /etc/tartest and not /etc/tar: mkfs.sh's convention, kept even though /bin/tar would not
# have collided with a path under /etc.
"$b6fsutil" -a tar.img /etc/tartest "$srcdir/tar.sh"

"$b6fsutil" -S --volume=3099 tar.img root3099.disk

# scratch3100.disk is NOT made here: `attach -n' in tar.ini creates and formats it.  It is
# removed above so that a rerun cannot inherit the last run's archive and pass without
# writing anything -- run-mkfs.sh's reason exactly.

if ! "$besm6" "$srcdir/tar.ini" >tar.console 2>&1; then
    cat tar.console >&2
    exit 1
fi
cat tar.console

#
# Oracle 4, and oracle 3's input.
#
"$b6fsutil" -S scratch3100.disk scratch-tar.img | tee tar.convert

# THE PACK COMES BACK CALLING ITSELF 3099, WHICH IS THE ROOT'S NUMBER, AND THAT IS A KERNEL
# DEFECT THIS TEST FOUND -- kernel/TODO.md task 37.  mdvol[] holds the volume mark a write
# stamps into the sector header, because the service-word buffer is the CONTROLLER's and the
# mark is the DRIVE's; but md.c fills it only from a completed READ, and treats zero as "not
# seen yet" and leaves the header alone -- which does not leave it blank, it leaves whatever
# the last read of any drive on that controller put there.  tar is the first program in this
# tree to write a pack it never reads (mkfs probes the last block first, fsck reads what it
# repairs), so it is the first to see it.
#
# The check is written round the wrong way ON PURPOSE, in kernel/test/fsck.sh's `hostblind'
# style: it requires the DEFECT, so that the day task 37 is fixed this fails and has to be
# tightened rather than quietly starting to pass.  A deferral said out loud is the difference
# between a known gap and an unknown one.
if grep -q 'volume 3100' tar.convert; then
    echo "run-tar.sh: the scratch pack now labels itself 3100 -- kernel/TODO.md task 37" >&2
    echo "  is FIXED.  Change this test to require 'volume 3100', delete this branch and" >&2
    echo "  the note beside it, and strike task 37 from kernel/TODO.md." >&2
    exit 1
fi
if ! grep -q 'volume 3099' tar.convert; then
    echo "run-tar.sh: the scratch pack came back as neither 3100 (fixed) nor 3099 (the" >&2
    echo "  known defect of kernel/TODO.md task 37).  Something else is stamping the" >&2
    echo "  sector header; kernel/dev/md.c's mdvol[] and cmd/mkfs/README.md SS2." >&2
    cat tar.convert >&2
    exit 1
fi
echo "run-tar.sh: the pack labels itself 3099 -- kernel/TODO.md task 37, still open"

#
# Oracle 3.  The host's tar, on the raw pack.
#
if tar tf scratch-tar.img >tar.rawnames 2>/dev/null; then
    LC_ALL=C sort tar.rawnames >tar.listing
    # SIX MEMBERS AND NOT NINE: v7's tar writes a header for a FILE and never for a
    # directory -- putfile() recurses on the directory branch and returns without a record of
    # its own -- so `tree', `tree/sub' and `tree/sub/deeper' exist in the archive only as
    # prefixes of the names under them, and are made on extraction by checkdir().  A modern
    # tar stores directory entries; tar.1's NOTES records the difference.
    #
    # LC_ALL=C on the sort, because the guest's sort(1) orders by BYTE and a UTF-8 locale on
    # the host does not: `tree/привет' sorts last one way and first the other.
    cat >tar.rawwant <<'EOF'
tree/big
tree/hardlink
tree/one
tree/sub/deeper/three
tree/sub/two
tree/привет
EOF
    if ! diff -u tar.rawwant tar.listing; then
        echo "run-tar.sh: the host's tar read the pack but not the archive that was" >&2
        echo "  written to it.  The records are on the device at the right offsets or" >&2
        echo "  tar would not have parsed a header at all, so suspect the WALK rather" >&2
        echo "  than the device: what the guest listed is in the ---raw--- section of" >&2
        echo "  tar.console above." >&2
        exit 1
    fi
    echo "run-tar.sh: the host's tar read all 6 members off the bare /dev/rmd1"
else
    # The archive is on the pack either way and the guest listed it back in ---raw---; this
    # oracle is the independent reader and is worth having, but a host without a tar that
    # tolerates a v7 header must SAY it skipped rather than pass quietly.
    echo "run-tar.sh: WARNING -- the host's tar would not read scratch-tar.img;" >&2
    echo "  oracle 3 skipped.  The guest's own listing in ---raw--- still stands." >&2
fi

#
# ... and now the root image, which oracles 1, 2 and 5 want.
#
"$b6fsutil" -S root3099.disk tarafter.img
"$b6fsutil" -c -v tarafter.img | tee tar.rootcheck

mkdir tar.out
"$b6fsutil" -x tarafter.img tar.out

#
# Oracle 2.  What the extraction restored, off the volume.
#
"$b6fsutil" -v -v tarafter.img >tar.check

# The mode and owner of two extracted files, and NOT of the corpus they came from.
for spec in '/tmp/back/tree/one:600' '/tmp/back/tree/sub/two:755'; do
    path=$(echo "$spec" | sed 's/:.*//')
    mode=$(echo "$spec" | sed 's/.*://')
    line=$(grep " $path\$" tar.check || true)
    if [ -z "$line" ]; then
        echo "run-tar.sh: $path is not on the image -- the extraction did not make it" >&2
        exit 1
    fi
    got=$(echo "$line" | awk '{ print $2 }')
    if [ "$got" != "$mode" ]; then
        echo "run-tar.sh: $path came back mode $got, the archive stored $mode" >&2
        echo "  tar creat()s with st_mode & 07777 out of the header, so suspect the mode" >&2
        echo "  field -- it is eight bytes at offset 100 and cmd_tar_listv pins it." >&2
        exit 1
    fi
done
echo "run-tar.sh: the extracted files carry the modes the archive stored"

# The hard link: one inode under two names, with nlink 2.
a=$(grep ' /tmp/back/tree/one$' tar.check | sed -n 's/.* ino \([0-9]*\) .*/\1/p')
b=$(grep ' /tmp/back/tree/hardlink$' tar.check | sed -n 's/.* ino \([0-9]*\) .*/\1/p')
n=$(grep ' /tmp/back/tree/one$' tar.check | sed -n 's/.* nlink \([0-9]*\).*/\1/p')
if [ -z "$a" ] || [ "$a" != "$b" ]; then
    echo "run-tar.sh: the extracted /tmp/back/tree/one (ino $a) and hardlink (ino $b)" >&2
    echo "  are not the same inode.  tar stores the second name it meets as linkflag '1'" >&2
    echo "  with the first name in linkname, and doxtract() calls link(2) for it; a" >&2
    echo "  separate inode means the linkbuf chain did not match on st_ino/st_dev." >&2
    exit 1
fi
if [ "$n" != "2" ]; then
    echo "run-tar.sh: the extracted hard link has nlink $n, expected 2" >&2
    exit 1
fi
echo "run-tar.sh: the hard link came back as one inode (ino $a) with nlink 2"

#
# Oracle 5.
#
"$b6fsutil" -c tarafter.img >tar.check2 || {
    cat tar.check2 >&2
    exit 1
}

#
# Oracle 1.
#
diff -u "$srcdir/tar.expected" tar.out/tmp/tar.log
