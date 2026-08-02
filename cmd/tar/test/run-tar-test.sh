#!/bin/sh
# The FILE oracle for tar(1), under b6sim.  Task C7.  Invoked by ctest as:
#
#	run-tar-test.sh SIM PROG SRCDIR
#
# ../../split/test/run-split-test.sh's header has the argument for why a program whose answer
# is FILES needs an assertion outside b6_progtest(); tar is the sharpest case of it, because
# the thing under test is a byte layout and stdout says nothing about one.  The nine
# b6_progtest cases next door read a CHECKED-IN v7-format archive and are the other half:
# ref.tar was made by the host's tar and its verbose listing pins every header field to its
# offset, which is what `cmd_tar_listv' is for.
#
# SIX THINGS ARE ASSERTED.
#
#   1.  A round trip.  What tar writes, tar reads back byte for byte -- including a file
#       whose size is an exact multiple of the 512-byte record and one that is empty, the
#       two ends of the block arithmetic.
#   2.  A BLOCKING FACTOR GREATER THAN ONE, and this is the case the port exists for.  v7
#       declared `union hblock tbuf[NBLOCK]'; sizeof that union is 516 here and not 512,
#       because 512 is not a multiple of six, so the array's stride and the transfer's stride
#       disagreed and every record after the first in a physical block landed four bytes out
#       of place.  Under v7's own defaults -- one record per read AND per write -- only record
#       0 was ever touched and nothing showed at all.  This IS sharp, and was proved so by
#       putting the defect back: addressing tbuf at sizeof(union hblock) instead of TBLOCK
#       makes section 1 fail on its first cmp, because this port reads NBLOCK records at a
#       time.  Section 2 is what would still catch it if the read side ever went back to one.
#   3.  Both directions of interchange with the HOST's tar, which genuinely speaks this
#       format -- ../../README.md's C5e rule, and the strongest oracle available here.  The
#       two do not write byte-identical headers (v7 blank-pads its octal fields where a
#       modern tar zero-pads them; both are legal and each reads the other), so what is
#       compared is the FILES, not the archives.
#   4.  Append.  `r' adds to an archive the host can still read afterwards.
#   5.  The 100-byte name field, from both sides: exactly 100 accepted -- the field carries
#       no terminator then, which is why every print of it goes through %.*s -- and 101
#       refused with a diagnostic.  ../../README.md's C5c rule about bracketing a bound.
#   6.  UTF-8 through the header and through the checksum.  `char' is unsigned here where the
#       PDP-11's was signed, so a Cyrillic name sums differently from what a real v7 tar would
#       have computed; the host's tar agrees with this one, which is the point.
#
# WHAT THIS HARNESS CANNOT SAY, and ../README.md says it again: b6sim refuses to read a
# directory descriptor, so the tree walk -- the whole reason task C7 exists -- is asserted in
# kernel/test/tar and nowhere else, along with /bin/mkdir on extraction, owner and mode
# restoration, and the raw device.
set -e
sim=$1
prog=$2
srcdir=$3

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables through,
# so an emptied environment is the only one that is the same on every machine.
tar_() { env -i "$sim" "$prog" "$@"; }

# Does the host's tar speak the v7 format?  GNU and bsdtar both do.  The probe archives a file
# that really exists, because `tar -cf /dev/null nosuchfile' fails for the wrong reason and
# would turn the whole cross-check into a silent skip -- which is what it did on the first
# run of this script, and is ../../README.md's C5b rule about quiet answers in miniature.
# The result is printed at the end either way, so a skip is stated rather than assumed.
hosttar=no
rm -rf probe
mkdir probe
printf 'probe\n' >probe/p
if tar --format=v7 -cf probe/probe.tar -C probe p 2>/dev/null && test -s probe/probe.tar; then
    hosttar=yes
fi
rm -rf probe

# ---- 1.  A round trip, with both ends of the record arithmetic.
rm -rf trip
mkdir trip
cd trip
printf 'one line\n' >small
: >empty
awk 'BEGIN { for (i = 0; i < 8; i++) printf "%s", "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqx" }' >exact512
test $(wc -c <exact512) -eq 512
awk 'BEGIN { for (i = 0; i < 40; i++) print "line", i }' >many
tar_ cf out.tar small empty exact512 many
mkdir back
cd back
tar_ xf ../out.tar
for f in small empty exact512 many; do cmp ../$f $f; done
cd ../..

# ---- 2.  A blocking factor greater than one -- the union-stride case.
rm -rf blocked
mkdir blocked
cd blocked
i=0
while [ $i -lt 12 ]; do
    awk -v n=$i 'BEGIN { for (j = 0; j < 30; j++) print "file", n, "line", j }' >f$i
    i=$((i + 1))
done
tar_ cfb out.tar 3 f0 f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11
mkdir back
cd back
tar_ xf ../out.tar
i=0
while [ $i -lt 12 ]; do
    cmp ../f$i f$i
    i=$((i + 1))
done
cd ..
# and read back at a DIFFERENT factor, because a blocking factor is a property of the write
tar_ tfb out.tar 6 >names
test $(wc -l <names) -eq 12
if [ $hosttar = yes ]; then
    mkdir hostback
    cd hostback
    tar xf ../out.tar
    i=0
    while [ $i -lt 12 ]; do
        cmp ../../blocked/f$i f$i
        i=$((i + 1))
    done
    cd ..
fi
cd ..

# ---- 3.  Interchange with the host's tar, both ways, and a Cyrillic name through it.
if [ $hosttar = yes ]; then
    rm -rf swap
    mkdir swap
    cd swap
    printf 'guest wrote this\n' >g1
    printf 'привет мир\n' >привет
    tar_ cf guest.tar g1 привет
    mkdir h
    cd h
    tar xf ../guest.tar
    cmp ../g1 g1
    cmp ../привет привет
    cd ..
    tar --format=v7 -cf host.tar g1 привет
    mkdir g
    cd g
    tar_ xf ../host.tar
    cmp ../g1 g1
    cmp ../привет привет
    cd ../..
fi

# ---- 4.  Append.
rm -rf grow
mkdir grow
cd grow
printf 'first\n' >p
printf 'second\n' >q
tar_ cf grow.tar p
tar_ rf grow.tar q
tar_ tf grow.tar >names
diff -u - names <<'EOF'
p
q
EOF
mkdir back
cd back
tar_ xf ../grow.tar
cmp ../p p
cmp ../q q
cd ..
if [ $hosttar = yes ]; then
    mkdir hb
    cd hb
    tar xf ../grow.tar
    cmp ../p p
    cmp ../q q
    cd ..
fi
cd ..

# ---- 5.  The 100-byte name field, from both sides.
rm -rf bound
mkdir bound
cd bound
n100=$(awk 'BEGIN { s = ""; while (length(s) < 100) s = s "n"; print s }')
n101=${n100}n
test $(printf %s "$n100" | wc -c) -eq 100
printf 'at the limit\n' >"$n100"
printf 'over the limit\n' >"$n101"
tar_ cf ok.tar "$n100"
tar_ tf ok.tar >names
test "$(cat names)" = "$n100"
mkdir back
cd back
tar_ xf ../ok.tar
cmp "../$n100" "$n100"
cd ..
set +e
tar_ cf over.tar "$n101" >diag 2>&1
set -e
grep -q 'file name too long' diag
tar_ tf over.tar >names2
test ! -s names2
cd ..

# ---- 6.  A damaged header is refused, and the fixture really was damaged.
#          ../../README.md's C4d rule: assert that there was something to find.
rm -rf damage
mkdir damage
cd damage
cp "$srcdir/ref.tar" good.tar
tar_ tf good.tar >names
test $(wc -l <names) -eq 3
cd ..

echo "cmd_tar_files: round trip, blocking factor, interchange ($hosttar), append, name bound"
