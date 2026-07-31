#!/bin/sh
#
# icheck's salvage case under b6sim: break a free list, rebuild it, and make three other
# implementations agree that what came back is sound.
#
#	run-icheck-test.sh SIM B6FSUTIL ICHECK FSCK SRCDIR CASE FIXTURE
#
# WHY THIS IS NOT b6_progtest().  That harness runs a program once and diffs its stdout;
# `icheck -s' is the only thing in task C4e that WRITES, so what has to be asserted is the
# state of the filesystem before and after, and by somebody other than the program under
# test.  ../../fsck/test/run-fsck-test.sh is the shape and its rules are the rules here.
#
# SIX ORACLES, and the last two are what this case exists for:
#
#   1. THE DAMAGE IS REAL.  `b6fsutil -c' must FAIL before icheck runs.  Without this a
#      damage spec that had drifted out of step with icheckimg.manifest would leave a case
#      that salvages nothing and passes -- and a clean image salvaged to a clean image looks
#      exactly like success.  ../../fsck/README.md SS5.
#   2. what icheck said, against a checked-in transcript.  It says almost nothing: v7's -s
#      is silent, and this port did not invent a banner for it.
#   3. `b6fsutil -c -v' must SUCCEED afterwards.  The host's checker is a separate
#      implementation in C++ that shares no line with the guest, so this is the "require
#      both to report the same thing" of ../../TODO.md.
#   4. icheck run again finds `missing    0' and a free count equal to the host's, converted
#      by KBPB.  A salvage that lost blocks would still pass 3.
#   5. fsck(1M) -- the OTHER free-list rebuilder on this image -- reads what icheck wrote and
#      finds nothing to do.  Two independently written makefree()s, one's output accepted by
#      the other's freechk().
#   6. and, the strongest one available: salvage a second copy with `fsck -s' instead and
#      `cmp' the two images BYTE FOR BYTE.  Both walk fmax-1 down to fmin skipping used
#      blocks, both flush a chain block at NICFREE, both write s_tinode as imax-nfiles-1 --
#      so if the two disagree anywhere at all, one of them is wrong.  The only field that
#      cannot match is s_time, which each stamps for itself, and `b6fsutil -D sb.time=0'
#      normalises it on both sides.  C4c's cmp-not-field-diff oracle, in the cheap world.
#
# WHAT THIS STILL CANNOT SAY is ../../df/README.md's warning unchanged: b6sim's read(2) and
# write(2) are the host's, so none of the five conditions of the raw path exists here.
# Everything green in this directory says the ARITHMETIC is right and nothing whatever about
# the device.  Task C4e has no SIMH test -- see ../../TODO.md -- so for these four programs
# that half is not asserted anywhere yet.
#
set -e

sim="$1"
b6fsutil="$2"
icheck="$3"
fsck="$4"
srcdir="$5"
case="$6"
fixture="$7"

KBPB=3 # 1024-byte blocks per 3072-byte filesystem block; ../../README.md SS4

img="$case.img"
alt="$case.alt.img"
rm -f "$img" "$alt" "$case.out" "$case.again" "$case.check" "$case.fsck"
cp "$fixture" "$img"
cp "$fixture" "$alt"

# The damage.  One spec per line, comments and blank lines skipped, so a .damage file can
# say why.  b6fsutil -D resolves the field names itself -- no test script here computes a
# block or a word offset, which is ../../TODO.md's rule for on-disk constants.
specs=$(grep -v '^[ 	]*#' "$srcdir/$case.damage" | grep -v '^[ 	]*$')
for spec in $specs; do
    "$b6fsutil" -D "$spec" "$img"
    "$b6fsutil" -D "$spec" "$alt"
done

# 1.
if "$b6fsutil" -c "$img" >"$case.before" 2>&1; then
    echo "FAIL: $case: b6fsutil finds nothing wrong with the damaged image." >&2
    echo "The damage spec has stopped matching the fixture -- check icheckimg.manifest" >&2
    echo "against $case.damage.  A case that salvages nothing would otherwise pass." >&2
    exit 1
fi
cat "$case.before"

# The guest.  env -i because b6sim forwards a whitelist of host variables.
env -i "$sim" "$icheck" -s "$img" >"$case.out" 2>&1
cat "$case.out"

# 2.
if ! diff -u "$srcdir/$case.expected" "$case.out"; then
    echo "FAIL: $case: icheck said something else." >&2
    exit 1
fi

# 3.
if ! "$b6fsutil" -c -v "$img" >"$case.check" 2>&1; then
    cat "$case.check" >&2
    echo "FAIL: $case: icheck said it was done and b6fsutil disagrees." >&2
    exit 1
fi
cat "$case.check"

# 4.  The host counts filesystem blocks and icheck reports 1024-byte ones, so the conversion
#     happens here, once -- kernel/test/run-fsck.sh's arrangement.
env -i "$sim" "$icheck" "$img" >"$case.again" 2>&1
cat "$case.again"
if ! grep -q '^missing    0$' "$case.again"; then
    echo "FAIL: $case: blocks went missing across the salvage." >&2
    exit 1
fi
blocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' "$case.check")
if [ -z "$blocks" ]; then
    echo "FAIL: $case: b6fsutil -c -v printed no block accounting." >&2
    exit 1
fi
want=$((blocks * KBPB))
got=$(sed -n 's/^free  *\([0-9]*\)$/\1/p' "$case.again")
if [ "$got" != "$want" ]; then
    echo "FAIL: $case: icheck says $got free 1K-blocks and the host's own walk of the" >&2
    echo "  same free list says $want ($blocks filesystem blocks x $KBPB)." >&2
    exit 1
fi
echo "run-icheck-test.sh: icheck and b6fsutil agree on $want free 1K-blocks"

# 5.
env -i "$sim" "$fsck" -n "$img" >"$case.fsck" 2>&1
cat "$case.fsck"
if grep -q '?' "$case.fsck"; then
    echo "FAIL: $case: fsck PROPOSED A REPAIR to the list icheck -s just laid down." >&2
    echo "  Every question fsck asks is a real inconsistency; the two makefree()s have" >&2
    echo "  diverged.  cmd/icheck/README.md is the account of what they share." >&2
    exit 1
fi

# 6.
env -i "$sim" "$fsck" -s -y "$alt" >"$case.altout" 2>&1
if ! "$b6fsutil" -c "$alt" >/dev/null 2>&1; then
    cat "$case.altout" >&2
    echo "FAIL: $case: fsck -s did not produce a sound filesystem either." >&2
    exit 1
fi
# s_time is the one word each program stamps for itself; nothing else may differ.
"$b6fsutil" -D sb.time=0 "$img" >/dev/null
"$b6fsutil" -D sb.time=0 "$alt" >/dev/null
if ! cmp "$img" "$alt"; then
    echo "FAIL: $case: icheck -s and fsck -s left DIFFERENT filesystems behind." >&2
    echo "  Both rebuild the free list descending from fmax-1, flush a chain block at" >&2
    echo "  NICFREE and derive s_tinode as imax-nfiles-1, so a byte that differs is a" >&2
    echo "  real disagreement between the two.  cmd/icheck/README.md." >&2
    exit 1
fi
echo "run-icheck-test.sh: icheck -s and fsck -s agree byte for byte"

exit 0
