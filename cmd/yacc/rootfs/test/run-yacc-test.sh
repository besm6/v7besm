#!/bin/sh
# Two yaccs built from one source must generate the same parser.  Task C10c,
# ../../../README.md; ../../README.md, "Testing the native build", has the argument.
# Invoked by ctest as:
#
#	run-yacc-test.sh HOSTYACC SIM PROG YACCPAR SRCDIR NAME GRAMMAR [flags...]
#
# HOSTYACC is the freshly built build/cmd/yacc/b6yacc, not an installed one; PROG is
# the native yacc AS STAGED FOR THE IMAGE.  YACCPAR is named explicitly because both
# search paths miss in this tree -- the host's ends at an installed file and the
# native one is /usr/lib/yaccpar, which under b6sim is the build machine's.
#
# Compared: y.tab.c, y.tab.h and the message stream, which carries the conflict
# counts and no argv[0].  NOT y.output: its statistics quote the size profile's own
# bounds, so it is the one output the two builds cannot agree on.
#
# The grammar is copied in under a fixed relative name -- it lands in every `# line'
# directive -- and each case gets a directory of its own, y.tab.c being a fixed name
# that would race under ctest -j.  A live diff and not a checked-in .expected: the
# property is agreement between two builds, which an expectation cannot express.
set -e
hostyacc=$1
sim=$2
prog=$3
yaccpar=$4
srcdir=$5
name=$6
grammar=$7
shift 7

rm -rf "$name.dir"
mkdir -p "$name.dir/a" "$name.dir/b"
cd "$name.dir"

cp "$grammar" a/g.y
cp "$grammar" b/g.y

set +e
(cd a && B6YACCPAR="$yaccpar" "$hostyacc" "$@" g.y) > host.log 2>&1
hoststatus=$?
(cd b && env -i B6YACCPAR="$yaccpar" "$sim" "$prog" "$@" g.y) > native.log 2>&1
nativestatus=$?
set -e

if [ "$hoststatus" != "$nativestatus" ]; then
    echo "$name: host exited $hoststatus, native $nativestatus" >&2
    cat host.log native.log >&2
    exit 1
fi

diff -u host.log native.log
diff -u a/y.tab.c b/y.tab.c
if [ -f a/y.tab.h ] || [ -f b/y.tab.h ]; then
    diff -u a/y.tab.h b/y.tab.h
fi
