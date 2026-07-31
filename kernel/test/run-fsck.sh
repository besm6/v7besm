#!/bin/sh
# fsck(1M) repairing a filesystem over the raw device -- the host half of kernel/test/fsck.
# Task C4d.
#
# Invoked by ctest as:
#
#	run-fsck.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-mkfs.sh's shape: copy the pristine image, graft the guest script onto the
# copy, convert it to a SIMH container at this test's own volume number, boot with a SECOND
# drive attached, then ask the host the questions the guest cannot answer about itself.
#
# What is different from mkfs is that the second pack arrives with a filesystem ALREADY ON
# IT, built here and then deliberately broken.  That is the whole shape of the test: the
# host breaks it, the guest fixes it, and the host -- whose checker is a separate
# implementation in C++ that shares no line of code with the guest's -- has to agree that
# what came back is sound.  cmd/TODO.md's "require both to report the same thing".
#
# SIX ORACLES, AND THE THIRD IS THE TASK.
#
#   1.  THE DAMAGE IS REAL.  `b6fsutil -c' must FAIL on the pack before it is converted.
#       Without this a damage spec that had drifted out of step with fsck.manifest would
#       leave a test that repairs nothing and passes -- and there is no other way to notice,
#       since a clean image repaired to a clean image looks exactly like success.
#
#   2.  THE SCRATCH PACK IS STILL ITSELF.  run-mkfs.sh's oracle 1, unchanged and for the
#       same reason: the sector-header service words are per CONTROLLER, kernel/dev/md.c
#       maintains only the address in them, so a written zone carries the volume number of
#       whichever pack was last READ.  b6fsutil reads it from zone 0 -- block 0 -- alone.
#       fsck cannot write block 0 (fmin is at least 2), and this is what holds it to that.
#
#   3.  THE REPAIR IS SOUND.  `b6fsutil -c -v' over the converted pack, five passes,
#       exit 0.  A filesystem the guest repaired through physio(), mdstrategy() and
#       mdwrite() that the host's independent checker cannot fault.
#
#   4.  THE TWO BLOCK ACCOUNTINGS AGREE, recomputed rather than remembered -- run-fsinfo.sh's
#       rule.  fsck's own `N blocks N free' from the second, read-only pass, against
#       `b6fsutil -c -v's block accounting times KBPB.  They really are the same two
#       quantities: fsck's pass1 claims every block an inode reaches INCLUDING its indirect
#       blocks, and check.cpp's walk_indirect() claims them too.
#
#   5.  THE LIVE ROOT.  fsck read the filesystem it was running on and found nothing to do:
#       no `?' prompt was printed, so nothing was proposed, and no MODIFIED banner, so
#       nothing was written.  And its free count equals df's, measured seconds apart by two
#       programs walking the same list -- and equals the host's reading of the image that
#       came back, which is the same claim from outside the machine.
#       This section is GREPPED rather than diffed, unlike oracle 6, because its numbers
#       are the root image's and change whenever anything joins /bin -- run-fsinfo.sh's
#       rule.  Note what the `?' check below therefore covers for free: the root has been
#       written to since the image was built, so s_tfree and s_tinode are only right if the
#       kernel really did maintain them across a whole boot's worth of allocation, and a
#       wrong one is a FIX prompt like any other.  Until kernel/alloc.c took the two fields
#       up they could not be asserted at all.
#
#   6.  THE LOG, diffed against fsck.expected with NOTHING MASKED.  OWNER= is real here --
#       getpw(3) reads the image's /etc/passwd, so uid 7 is `guest' every time -- which is
#       the one thing cmd/fsck/test cannot say, its /etc/passwd being the build machine's.
#
# Plus `b6fsutil -c' on the root image too, which every writing test here ends with.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

NBLK=2000       # the scratch pack: one whole EC-5052, so phase 6 has a real free list to lay
FSTIME=1700000000
KBPB=3          # 1024-byte blocks per 3072-byte filesystem block; cmd/README.md SS4

rm -rf fsck.img root3091.disk scratch3092.disk scratch.img scratchafter.img \
       fsckafter.img fsck.out fsck0.drum fsck1.drum fsck.console fsck.check \
       fsck.rootcheck scratch.convert scratch.before

#
# The scratch pack: build it, break it, and check that it really is broken.
#
"$b6fsutil" -n -s $NBLK -T $FSTIME -M "$srcdir/fsck.manifest" scratch.img

specs=$(grep -v '^[ 	]*#' "$srcdir/simh.damage" | grep -v '^[ 	]*$')
for spec in $specs; do
    "$b6fsutil" -D "$spec" scratch.img
done

# Oracle 1.
if "$b6fsutil" -c scratch.img >scratch.before 2>&1; then
    echo "run-fsck.sh: b6fsutil finds nothing wrong with the image it was just told to" >&2
    echo "  break.  simh.damage has stopped matching fsck.manifest -- the i-numbers in it" >&2
    echo "  are that file's order.  Left alone, this test would repair nothing and pass." >&2
    cat scratch.before >&2
    exit 1
fi
cat scratch.before

"$b6fsutil" -S --volume=3092 scratch.img scratch3092.disk

#
# The root, with the guest script grafted in.
#
cp root.img fsck.img
# /etc/fscktest and NOT /etc/fsck: the latter is the program under test, and `-a' would
# write the script straight over it.  fsck.sh's header says so too.
"$b6fsutil" -a fsck.img /etc/fscktest "$srcdir/fsck.sh"
"$b6fsutil" -S --volume=3091 fsck.img root3091.disk

# The transcript is an oracle here, so it is kept -- but it is also where a failing run says
# what went wrong, so it is echoed either way.
if ! "$besm6" "$srcdir/fsck.ini" >fsck.console 2>&1; then
    cat fsck.console >&2
    exit 1
fi
cat fsck.console

#
# Oracle 2.
#
"$b6fsutil" -S scratch3092.disk scratchafter.img | tee scratch.convert
if ! grep -q 'volume 3092' scratch.convert; then
    echo "run-fsck.sh: the scratch pack came back claiming to be some other volume." >&2
    echo "  kernel/dev/md.c leaves the volume number in a written zone's service words as" >&2
    echo "  the last READ of any drive on the controller left it, and zone 0 track 0 --" >&2
    echo "  block 0 -- is the only one b6fsutil reads it from.  Something wrote block 0," >&2
    echo "  which fsck should not be able to do at all: fmin is never below 2." >&2
    cat scratch.convert >&2
    exit 1
fi

#
# Oracle 3.
#
if ! "$b6fsutil" -c -v scratchafter.img | tee fsck.check; then
    echo "run-fsck.sh: the guest said it had repaired the pack and the host disagrees." >&2
    echo "  cmd_fsck_* next door makes the same repairs under b6sim, where the device" >&2
    echo "  cannot be at fault; if those pass and this does not, the write path is where" >&2
    echo "  to look -- physio(), mdstrategy(), mdwrite(), and the drainbrz() in mdstart()." >&2
    exit 1
fi

#
# ... and the root image, which the rest of the checks want.
#
"$b6fsutil" -S root3091.disk fsckafter.img
"$b6fsutil" -c -v fsckafter.img | tee fsck.rootcheck

mkdir fsck.out
"$b6fsutil" -x fsckafter.img fsck.out

#
# Oracle 4.  fsck's second, read-only pass over the repaired pack against the host's own
# walk of the same free list.  The two sides are in different units on purpose -- b6fsutil
# counts filesystem blocks, fsck reports 1024-byte ones -- and converting here, once, is
# run-fsinfo.sh's and run-mkfs.sh's arrangement.
#
again=$(sed -n '/^---again---$/,/^---bad---$/p' fsck.out/tmp/fsck.log)
blocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' fsck.check)
if [ -z "$blocks" ]; then
    echo "run-fsck.sh: b6fsutil -c -v printed no block accounting -- see fsck.check" >&2
    exit 1
fi
want=$((blocks * KBPB))
got=$(echo "$again" | sed -n 's/^[0-9]* files [0-9]* blocks \([0-9]*\) free$/\1/p')
if [ "$got" != "$want" ]; then
    echo "run-fsck.sh: on the repaired pack fsck says $got free 1K-blocks and the host's" >&2
    echo "  own free-list walk says $want ($blocks filesystem blocks x $KBPB)" >&2
    exit 1
fi
echo "run-fsck.sh: fsck and b6fsutil agree on $want free 1K-blocks for the repaired pack"

#
# Oracle 5.  The live root, out of the console transcript.  Console lines end in CR.
#
root=$(tr -d '\r' <fsck.console | sed -n '/^---root---$/,/^---endroot---$/p')
if [ -z "$root" ]; then
    echo "run-fsck.sh: nothing between ---root--- and ---endroot--- on the console" >&2
    exit 1
fi
echo "$root"

for want in '(NO WRITE)' '\*\* Phase 1' '\*\* Phase 2' '\*\* Phase 3' '\*\* Phase 4' \
            '\*\* Phase 5'; do
    if ! echo "$root" | grep -q -- "$want"; then
        echo "run-fsck.sh: the live root's check never printed '$want'" >&2
        exit 1
    fi
done
if echo "$root" | grep -q 'COUNT WRONG IN SUPERBLK'; then
    echo "run-fsck.sh: on the mounted root fsck found a superblock total wrong, so the" >&2
    echo "  kernel did not keep s_tfree/s_tinode across this boot's allocations." >&2
    echo "  Something allocates or frees outside alloc()/free()/ialloc()/ifree()" >&2
    echo "  (kernel/alloc.c), or one of the four counts in the wrong place." >&2
    exit 1
fi
if echo "$root" | grep -q '?'; then
    echo "run-fsck.sh: fsck PROPOSED A REPAIR to the mounted root, which should be sound." >&2
    echo "  Every question fsck asks is a real inconsistency it found.  If it is" >&2
    echo "  'N BLK(S) MISSING', read the warning at the head of fsck.sh: something was" >&2
    echo "  added below the ---root--- section and allocated a block after fsck's sync(2)." >&2
    exit 1
fi
if echo "$root" | grep -q 'MODIFIED'; then
    echo "run-fsck.sh: fsck WROTE to the mounted root.  It was run with -n and must not." >&2
    exit 1
fi

# The two guest numbers against each other: fsck walked the free list and so did df, seconds
# apart, on the same device.
fsckfree=$(echo "$root" | sed -n 's/^[0-9]* files [0-9]* blocks \([0-9]*\) free$/\1/p')
dffree=$(echo "$root" | sed -n 's|^/dev/rmd0 \([0-9]*\)$|\1|p')
if [ -z "$fsckfree" ] || [ "$fsckfree" != "$dffree" ]; then
    echo "run-fsck.sh: on the mounted root fsck says $fsckfree free and df says $dffree" >&2
    exit 1
fi

# ... and against the host's reading of the image that came back, which is the same claim
# made from outside the machine.  Nothing writes to the root between fsck's sync(2) and the
# end of the run -- fsck.sh's header is emphatic about keeping it that way -- so these are
# measurements of the same instant.
rootblocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' fsck.rootcheck)
if [ "$fsckfree" != "$((rootblocks * KBPB))" ]; then
    echo "run-fsck.sh: on the mounted root fsck says $fsckfree free 1K-blocks and the" >&2
    echo "  host's walk of the image that came back says $((rootblocks * KBPB))." >&2
    echo "  If this is off by a multiple of $KBPB, something allocated a block after" >&2
    echo "  fsck's sync(2); see the warning at the head of fsck.sh." >&2
    exit 1
fi
echo "run-fsck.sh: fsck, df and the host all put $fsckfree free 1K-blocks on the mounted root"

#
# Oracle 6.
#
diff -u "$srcdir/fsck.expected" fsck.out/tmp/fsck.log
