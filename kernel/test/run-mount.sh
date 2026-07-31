#!/bin/sh
# mount(1M) putting a second filesystem on the machine -- the host half of kernel/test/mount.
# Task C4f, and the last of task C4.
#
# Invoked by ctest as:
#
#	run-mount.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-fsck.sh's shape: build the scratch filesystem here, convert it to a SIMH
# container at this test's own volume number, copy the pristine root image and graft the
# guest script onto the copy, boot with both attached, then ask the host the questions the
# guest cannot answer about itself.
#
# WHAT IS DIFFERENT FROM fsck IS THE PATH THE WRITES TOOK.  There the guest repaired a pack
# through physio() -> mdstrategy() -> mdwrite(), which is where every byte task C4 has ever
# written went.  Here the pack is MOUNTED, and the files the guest puts on it go out through
# getblk(), bwrite() and bdwrite() -- the buffer cache, on bdevsw[0] minor 1, which had never
# carried a block.  Oracle 4 is the one that says so: the host has to find those files.
#
# SEVEN ORACLES.
#
#   1.  THE FIXTURE WAS SOUND TO BEGIN WITH.  `b6fsutil -c' must PASS on the pack before it
#       is converted.  This is run-fsck.sh's oracle 1 with the polarity reversed and for the
#       same reason: there the damage had to be real, here the starting point has to be
#       clean, or a fault at the end would say nothing about what the guest did.
#
#   2.  THE SCRATCH PACK IS STILL ITSELF.  run-mkfs.sh's and run-fsck.sh's oracle, and this
#       is the first time it is asked of the CACHE.  The sector-header service words are per
#       CONTROLLER (kernel/dev/md.c maintains only the address in them), so a written zone
#       carries the volume number of whichever pack was last READ, and b6fsutil reads it from
#       zone 0 track 0 -- block 0 -- alone.  A mounted filesystem cannot reach block 0:
#       SUPERB is block 1 and alloc() is bounded below by badblock().  This is what holds it
#       to that.
#
#   3.  WHAT CAME BACK IS A FILESYSTEM.  `b6fsutil -c -v', five passes, exit 0 -- over a pack
#       that was mounted, written through the cache, unmounted, deliberately broken with
#       clri, repaired by fsck and had its free list laid down again by `icheck -s'.
#
#   4.  THE BYTES REALLY ARRIVED, AND THIS IS THE TASK.  /copy is extracted from the pack and
#       `cmp'd against /etc/motd out of the root image that came back -- run-mkfs.sh's oracle
#       5, nothing checked in, nothing recomputed.  If the mount worked and the cache did not,
#       this is what says so.
#
#   5.  THE GUEST AND THE HOST AGREE ABOUT THE FREE SPACE, recomputed rather than remembered
#       (run-fsinfo.sh's rule): df's last reading of /dev/md1 -- taken through the MOUNT, so
#       through the cache -- against the host's own free-list walk of the pack, times KBPB.
#
#   6.  THE LIVE ROOT, and the console.  icheck and dcheck read the filesystem the machine is
#       running on and found nothing; and sbcheck() refused the drum, which is what retires
#       mount.1m's oldest BUGS line.
#
#   7.  THE LOG, diffed against mount.expected with NOTHING MASKED.
#
# Plus `b6fsutil -c' on the root image too, which every writing test here ends with.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

NBLK=2000       # the scratch pack: one whole EC-5052, as mkfs's and fsck's are
FSTIME=1700000000
KBPB=3          # 1024-byte blocks per 3072-byte filesystem block; cmd/README.md SS4

rm -rf mount.img root3093.disk scratch3094.disk blank3095.disk scratch.img \
       scratchafter.img mountafter.img mount.out mount.scratch mount0.drum mount1.drum \
       mount.console mount.check mount.rootcheck scratch.convert scratch.before

#
# The scratch pack: build it, and check that it really is sound.
#
"$b6fsutil" -n -s $NBLK -T $FSTIME -M "$srcdir/mount.manifest" scratch.img

# Oracle 1.
if ! "$b6fsutil" -c scratch.img >scratch.before 2>&1; then
    echo "run-mount.sh: the fixture this test mounts is already broken before the boot." >&2
    echo "  mount.manifest and b6fsutil disagree about something, and nothing the guest" >&2
    echo "  does below would be interpretable.  Fix this before reading any other failure." >&2
    cat scratch.before >&2
    exit 1
fi
cat scratch.before

"$b6fsutil" -S --volume=3094 scratch.img scratch3094.disk

#
# The root, with the guest script grafted in.
#
cp root.img mount.img
# /etc/mounttest and NOT /etc/mount: the latter is the program under test, and `-a' would
# write the script straight over it.  mount.sh's header says so too.
"$b6fsutil" -a mount.img /etc/mounttest "$srcdir/mount.sh"
"$b6fsutil" -S --volume=3093 mount.img root3093.disk

# The transcript is an oracle here, so it is kept -- but it is also where a failing run says
# what went wrong, so it is echoed either way.
if ! "$besm6" "$srcdir/mount.ini" >mount.console 2>&1; then
    cat mount.console >&2
    exit 1
fi
cat mount.console

#
# Oracle 2.
#
"$b6fsutil" -S scratch3094.disk scratchafter.img | tee scratch.convert
if ! grep -q 'volume 3094' scratch.convert; then
    echo "run-mount.sh: the scratch pack came back claiming to be some other volume." >&2
    echo "  kernel/dev/md.c leaves the volume number in a written zone's service words as" >&2
    echo "  the last READ of any drive on the controller left it, and zone 0 track 0 --" >&2
    echo "  block 0 -- is the only one b6fsutil reads it from.  Something wrote block 0." >&2
    echo "  From a MOUNTED filesystem that should be impossible: SUPERB is block 1 and" >&2
    echo "  badblock() (kernel/alloc.c) bounds every allocation below by s_isize." >&2
    cat scratch.convert >&2
    exit 1
fi

#
# Oracle 3.
#
if ! "$b6fsutil" -c -v scratchafter.img | tee mount.check; then
    echo "run-mount.sh: the guest mounted this pack, wrote to it and unmounted it, and the" >&2
    echo "  host cannot make sense of what came back.  The writes went through the BUFFER" >&2
    echo "  CACHE and not physio(), which is what this test exists for and what nothing" >&2
    echo "  else here exercises on a second drive: look at getblk(), bwrite() and bflush()" >&2
    echo "  with a b_dev of minor 1 before suspecting the filesystem code." >&2
    exit 1
fi

#
# ... and the root image, which the rest of the checks want.
#
"$b6fsutil" -S root3093.disk mountafter.img
"$b6fsutil" -c -v mountafter.img | tee mount.rootcheck

mkdir mount.out mount.scratch
"$b6fsutil" -x mountafter.img mount.out
"$b6fsutil" -x scratchafter.img mount.scratch

#
# Oracle 4.  THE TASK.  The guest copied /etc/motd onto the mounted filesystem with cp(1);
# every block of it went out through the cache onto minor 1.  The original comes out of the
# root image that came back rather than from the build tree, so nothing here is checked in.
#
if [ ! -f mount.scratch/copy ]; then
    echo "run-mount.sh: /copy is not on the pack.  The guest's cp(1) reported nothing" >&2
    echo "  (mount.expected would have caught that), so the blocks were written and lost:" >&2
    echo "  suspect bdwrite()/bflush() on minor 1, or an unmount that dropped them." >&2
    ls -l mount.scratch >&2
    exit 1
fi
if ! cmp mount.out/etc/motd mount.scratch/copy; then
    echo "run-mount.sh: /copy on the pack is not the /etc/motd the guest copied there." >&2
    echo "  The cache wrote SOMETHING to minor 1 and it is not what it was given." >&2
    exit 1
fi
echo "run-mount.sh: /copy came back byte for byte -- the buffer cache carried it to minor 1"

if [ ! -f mount.scratch/dir/hello ]; then
    echo "run-mount.sh: /dir/hello is not on the pack, so the mkdir(2) or the create did" >&2
    echo "  not survive the unmount.  cp(1) above did; this one is a directory block and" >&2
    echo "  an i-node rather than data blocks." >&2
    exit 1
fi

# ... and the other direction: the guest read /d/e/f back OFF the pack through the mount and
# left the copy on the root.  The host put that file there before the boot, so this is the
# cache carrying blocks IN rather than out, and both sides of the comparison come out of
# images that came back.
if ! cmp mount.out/etc/motd mount.out/tmp/fromdisk; then
    echo "run-mount.sh: what the guest read back through the mount is not what the host" >&2
    echo "  wrote onto the pack.  /d/e/f is mount.manifest's copy of etc/motd, and the" >&2
    echo "  guest cp'd it to /tmp/fromdisk while /dev/md1 was mounted.  Oracle 4 above" >&2
    echo "  passed, so the cache can WRITE minor 1; this is the read." >&2
    exit 1
fi

#
# Oracle 5.  The guest's last reading of the pack against the host's own walk of the image
# that came back.  Different units on purpose, converted here once, as run-fsinfo.sh and
# run-fsck.sh do.
#
# IT IS THE ---free--- READING AND NOT THE MOUNTED ONE, and mount.sh says why in the same
# words: clri threw a file away and fsck reclaimed its blocks, so the count df read through
# the mount is deliberately not the count the pack ends with.  The mounted reading is
# asserted as a literal by oracle 7's diff, which is the right instrument for a number the
# test itself put there.
#
blocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' mount.check)
if [ -z "$blocks" ]; then
    echo "run-mount.sh: b6fsutil -c -v printed no block accounting -- see mount.check" >&2
    exit 1
fi
free=$(sed -n '/^---free---$/,/^---end---$/p' mount.out/tmp/mount.log |
       sed -n 's|^/dev/rmd1 \([0-9]*\)$|\1|p')
want=$((blocks * KBPB))
if [ -z "$free" ]; then
    echo "run-mount.sh: no df line for /dev/rmd1 in the guest's ---free--- section" >&2
    exit 1
fi
if [ "$free" != "$want" ]; then
    echo "run-mount.sh: the guest's last df says $free free 1K-blocks on the pack and the" >&2
    echo "  host's own free-list walk says $want ($blocks filesystem blocks x $KBPB)." >&2
    echo "  Both are measurements of the same instant -- nothing touches the pack between" >&2
    echo "  that df and the end of the run -- so they disagree only if the free list the" >&2
    echo "  guest laid down with \`icheck -s' is not the one it can read back." >&2
    exit 1
fi
echo "run-mount.sh: the guest and the host both put $want free 1K-blocks on the pack"

#
# Oracle 6.  The console.  Lines end in CR.
#
console=$(tr -d '\r' <mount.console)

garbage=$(echo "$console" | sed -n '/^---garbage---$/,/^---endgarbage---$/p')
if ! echo "$garbage" | grep -q 'not a filesystem'; then
    echo "run-mount.sh: mounting /dev/swap did not make sbcheck() say 'not a filesystem'." >&2
    echo "  That refusal is what retires mount.1m's oldest BUGS line -- v7's mount(2)" >&2
    echo "  believes whatever superblock it reads, and this kernel does not.  If the guest" >&2
    echo "  saw EINVAL (mount.expected says it did) but nothing reached the console, look" >&2
    echo "  at sbcheck()'s prdev() in kernel/alloc.c." >&2
    echo "$garbage" >&2
    exit 1
fi

root=$(echo "$console" | sed -n '/^---root---$/,/^---endroot---$/p')
if [ -z "$root" ]; then
    echo "run-mount.sh: nothing between ---root--- and ---endroot--- on the console" >&2
    exit 1
fi
echo "$root"

# icheck's and dcheck's complaints, all of which are absences.  `missing' is the one the
# warning at the head of mount.sh is about.
for bad in 'MISSING' 'DUP' 'BAD;' 'bad;' 'blocks missing'; do
    if echo "$root" | grep -q -- "$bad"; then
        echo "run-mount.sh: on the mounted root icheck or dcheck reported '$bad'." >&2
        echo "  If it is about missing blocks, read the warning at the head of mount.sh:" >&2
        echo "  something was added below the ---root--- section and allocated a block" >&2
        echo "  after icheck's sync(2)." >&2
        echo "$root" >&2
        exit 1
    fi
done
if echo "$root" | grep -q 'SALVAG'; then
    echo "run-mount.sh: icheck tried to SALVAGE the mounted root.  It was run without -s" >&2
    echo "  and must not; see cmd/icheck/README.md SS5 -- that path stops the machine." >&2
    exit 1
fi

#
# Oracle 7.
#
diff -u "$srcdir/mount.expected" mount.out/tmp/mount.log
