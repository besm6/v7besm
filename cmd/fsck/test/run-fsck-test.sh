#!/bin/sh
#
# One fsck case under b6sim: damage a filesystem, repair it, and make the host agree.
#
#	run-fsck-test.sh SIM B6FSUTIL PROG SRCDIR CASE FIXTURE
#
# WHY THIS IS NOT b6_progtest().  Not because anything here has to be masked -- THE ORACLE
# IS FIVE ASSERTIONS and that harness makes one.  It did mask, once: pinode() maps a uid to
# a name with getpw(3), which opens the literal /etc/passwd, and under b6sim that used to be
# THE BUILD MACHINE's password file, so OWNER= was rewritten before the diff.  b6sim serves
# the target's /etc now (cmd/sim/etcfiles.cpp), the names below are the image's, and the diff
# is of raw output -- the same output kernel/test/fsck asserts under SIMH.
#
# THE FIVE ASSERTIONS, and the middle one is the task:
#
#   1. the damage is not a no-op -- `b6fsutil -c' must FAIL before fsck runs.  Without
#      this a spec that stopped matching the fixture would leave a case that repairs
#      nothing and passes.
#   2. what fsck said, against a checked-in transcript.
#   3. `b6fsutil -c' must SUCCEED afterwards.  The guest repaired it; the host, whose
#      checker is a separate implementation, cannot fault the result.  That is what
#      ../../README.md's "require both to report the same thing" comes to.
#   4. fsck run a second time must find nothing and modify nothing -- a repair that is not
#      idempotent is a repair that did not finish.
#   5. and it must exit 0 doing it.
#
# Four kinds of case, marked by which files are present beside the .expected:
#
#   (no .damage)     a CLEAN case: fsck runs -n, and 1 and 3 invert -- the image must be
#                    clean throughout, and fsck must agree with the host that it is.
#   .damage          the ordinary one: broken, repaired, clean.
#   .unrepairable    fsck is expected to REFUSE rather than fix -- a superblock it will not
#                    touch -- so 3 and 4 are skipped and the image must still be broken.
#   .hostblind       the two checkers deliberately disagree: the damage is real and
#                    `b6fsutil -c' does not look for it.  Assertion 1 inverts, and the
#                    image must be clean to the host both before and after while fsck says
#                    something about it and modifies nothing.  THERE IS NO SUCH CASE at
#                    present: `tcounts' was the one, and it stopped being one when the
#                    kernel took up s_tfree/s_tinode and check.cpp started faulting them
#                    (cmd/fsck/README.md SS1).  The machinery stays because the next
#                    divergence should be declared rather than papered over -- the message
#                    below is what walked this one out.
#
set -e

sim="$1"
b6fsutil="$2"
prog="$3"
srcdir="$4"
case="$5"
fixture="$6"

img="$case.img"
rm -f "$img" "$case.out" "$case.again"
cp "$fixture" "$img"

flag=-n
if [ -s "$srcdir/$case.damage" ]; then
    flag=-y

    # The damage.  One spec per line, comments and blank lines skipped, so a .damage file
    # can say why.  b6fsutil -D resolves the field names itself -- no test script here
    # computes a block or a word offset, which is ../../README.md's rule for on-disk
    # constants and the reason the verb is symbolic.
    specs=$(grep -v '^[ 	]*#' "$srcdir/$case.damage" | grep -v '^[ 	]*$')
    for spec in $specs; do
        "$b6fsutil" -D "$spec" "$img"
    done

    # 1.  ... and it really is damage, as the OTHER implementation sees it.
    if "$b6fsutil" -c "$img" >"$case.before" 2>&1; then
        if [ ! -f "$srcdir/$case.hostblind" ]; then
            echo "FAIL: $case: b6fsutil finds nothing wrong with the damaged image." >&2
            echo "The damage spec has stopped matching the fixture -- check fsckimg.manifest" >&2
            echo "against $case.damage.  A case that repairs nothing would otherwise pass." >&2
            exit 1
        fi
    elif [ -f "$srcdir/$case.hostblind" ]; then
        echo "FAIL: $case: b6fsutil now DOES see this damage." >&2
        echo "That is an improvement, not a failure -- drop $case.hostblind and update" >&2
        echo "cmd/fsck/README.md's list of what the two checkers disagree about." >&2
        exit 1
    fi
    cat "$case.before"
else
    if ! "$b6fsutil" -c "$img" >/dev/null 2>&1; then
        echo "FAIL: $case: the fixture is not clean to start with." >&2
        exit 1
    fi
fi

# The guest.  env -i because b6sim forwards a whitelist of host variables.
set +e
env -i "$sim" "$prog" $flag "$img" >"$case.out" 2>&1
status=$?
set -e

cat "$case.out"

# 2.  what it said.
if ! diff -u "$srcdir/$case.expected" "$case.out"; then
    echo "FAIL: $case: fsck said something else." >&2
    exit 1
fi

# 5.  and how it ended.
want=0
if [ -f "$srcdir/$case.status" ]; then
    want=$(cat "$srcdir/$case.status")
fi
if [ "$status" -ne "$want" ]; then
    echo "FAIL: $case: exit status $status, wanted $want." >&2
    exit 1
fi

if [ -f "$srcdir/$case.hostblind" ]; then
    # fsck saw something the host's checker does not look for, and must have left the
    # filesystem alone: a note is not a repair.
    if grep -q 'MODIFIED' "$case.out"; then
        echo "FAIL: $case: fsck wrote to the filesystem over a NOTE." >&2
        exit 1
    fi
    exit 0
fi

if [ -f "$srcdir/$case.unrepairable" ]; then
    # fsck was expected to refuse this one.  The image must still be broken: a refusal
    # that quietly repaired something would be worse than either outcome.
    if "$b6fsutil" -c "$img" >/dev/null 2>&1; then
        echo "FAIL: $case: fsck refused the image but the image came out clean." >&2
        exit 1
    fi
    exit 0
fi

# 3.  the host, independently, on what the guest left behind.
if ! "$b6fsutil" -c -v "$img"; then
    echo "FAIL: $case: fsck said it was done and b6fsutil disagrees." >&2
    exit 1
fi

# 4.  again, read-only: nothing left to find, nothing written.
env -i "$sim" "$prog" -n "$img" >"$case.again" 2>&1
if grep -q 'MODIFIED' "$case.again"; then
    cat "$case.again" >&2
    echo "FAIL: $case: the repair is not idempotent -- a second fsck wrote again." >&2
    exit 1
fi

exit 0
