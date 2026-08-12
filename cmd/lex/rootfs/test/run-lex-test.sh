#!/bin/sh
# Two lexes built from one source must generate the same scanner.  Task C10d,
# ../../../README.md; ../../README.md, "Testing the native build", has the argument.
# Invoked by ctest as:
#
#	run-lex-test.sh HOSTLEX SIM PROG NCFORM SRCDIR NAME SCANNER [flags...]
#
# HOSTLEX is the freshly built build/cmd/lex/b6lex, not an installed one; PROG is
# the native lex AS STAGED FOR THE IMAGE.  NCFORM is named explicitly because both
# search paths miss in this tree -- the host's ends at an installed file and the
# native one is /usr/lib/lex/ncform, which under b6sim is the build machine's.
#
# Compared: lex.yy.c and the message stream, which carries the warnings.  NOT the
# -v statistics: they quote the size profile's own bounds, so they are the one
# output the two builds cannot agree on, and no case passes -v.
#
# The scanner is copied in under a fixed relative name -- it lands in every
# diagnostic through sargv[fptr] -- and each case gets a directory of its own,
# lex.yy.c being a fixed name that would race under ctest -j.  A live diff and not
# a checked-in .expected: the property is agreement between two builds, which an
# expectation cannot express.
set -e
hostlex=$1
sim=$2
prog=$3
ncform=$4
srcdir=$5
name=$6
scanner=$7
shift 7

rm -rf "$name.dir"
mkdir -p "$name.dir/a" "$name.dir/b"
cd "$name.dir"

cp "$scanner" a/s.l
cp "$scanner" b/s.l

set +e
(cd a && B6LEXFORM="$ncform" "$hostlex" "$@" s.l) > host.log 2>&1
hoststatus=$?
(cd b && env -i B6LEXFORM="$ncform" "$sim" "$prog" "$@" s.l) > native.log 2>&1
nativestatus=$?
set -e

if [ "$hoststatus" != "$nativestatus" ]; then
    echo "$name: host exited $hoststatus, native $nativestatus" >&2
    cat host.log native.log >&2
    exit 1
fi

diff -u host.log native.log
diff -u a/lex.yy.c b/lex.yy.c
