#!/bin/sh
# ed(1) on a running system -- the host half of kernel/test/edit.  Task C3.
#
# Invoked by ctest as:
#
#	run-edit.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-session.sh's shape: copy the pristine image, graft the guest script onto the
# copy, convert it to a SIMH container at this test's own volume number, boot, convert what
# comes back, fsck it, extract it, and ask the host the questions the guest cannot answer
# about itself.
#
# THREE ORACLES, AND THE MIDDLE ONE IS THE POINT OF THE TASK.
#
#   1.  `b6fsutil -c' -- five passes over the inodes, the free list and the directory tree.
#       Structural: what ed wrote is still a filesystem.  Nearly free, and it is the reason
#       every writing test here ends this way.
#
#   2.  TWO BYTE-FOR-BYTE cmps, with no fixture checked in anywhere.  /etc/motd is 365 bytes
#       of UTF-8 Cyrillic, and v7's ed calls error() on any byte with 0200 set (getfile()), so
#       `ed /etc/motd' was a `?' and nothing else -- on a file cat has always printed.  The
#       guest read it and wrote it straight back to /tmp/motd.ed, and then did the same to
#       three copies of it, 1,095 bytes, which is long enough that lines land on both sides of
#       ed's own 512-byte temp blocks and getblock() has to splice one across two of them.
#       Comparing the copies against the originals OUT OF THE SAME EXTRACTED IMAGE means a
#       dropped eighth bit, a mis-spliced block or a lost line cannot pass, and nothing has to
#       be kept in step by hand.  session.sh's `cmp ls big' is the precedent.
#
#   3.  The log, diffed against edit.expected: the diagnostics, the line numbers `=' printed,
#       the byte counts `w' printed, the text ed produced, and a closed listing of /tmp.
#
# NOTHING IS MASKED HERE, and that is worth stating because run-utils.sh and run-files.sh both
# have to mask something.  ed prints arithmetic -- byte counts and line numbers -- rather than
# times or sizes, so every column of its output is reproducible as it stands.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf edit.img root3086.disk editafter.img edit.out edit0.drum edit1.drum
cp root.img edit.img
"$b6fsutil" -a edit.img /etc/edit "$srcdir/edit.sh"

"$b6fsutil" -S --volume=3086 edit.img root3086.disk

"$besm6" "$srcdir/edit.ini"

"$b6fsutil" -S root3086.disk editafter.img
"$b6fsutil" -c editafter.img

mkdir edit.out
"$b6fsutil" -x editafter.img edit.out

# Oracle 2, before the log: if these fail, the log's diff is the less interesting answer.
cmp edit.out/etc/motd edit.out/tmp/motd.ed
cat edit.out/etc/motd edit.out/etc/motd edit.out/etc/motd >edit.three
cmp edit.three edit.out/tmp/three.ed

# And the substituted copy is NOT identical -- it is the same file with one word replaced by a
# Cyrillic one on each of its three copies, so it must differ, and by more bytes than it has
# lines.  A `g' that matched nothing would leave it identical and pass silently otherwise.
if cmp -s edit.three edit.out/tmp/three.ed2; then
    echo "run-edit.sh: /tmp/three.ed2 is unchanged -- the global substitute matched nothing" >&2
    exit 1
fi

# Oracle 3.
diff -u "$srcdir/edit.expected" edit.out/tmp/edit.log
