#!/bin/sh
# Boot the kernel and have the machine build, link and run a program with its own toolchain
# -- the closing claim of task C9 (../../cmd/TODO.md).
#
# Invoked by ctest as: run-toolchain.sh B6FSUTIL BESM6 B6CC LIBCDIR RTDIR SRCDIR, with the
# kernel test BUILD directory as the working directory -- where root.img and ../unix already
# are.
#
# THE SAME SHAPE AS run-filters.sh and run-inspect.sh: a script rather than a chain of ctest
# fixtures, its own copy of the image grafted with `-a' rather than a line in
# ../../root.manifest, at its own SIMH volume (3102) so that nothing here writes a build
# artifact and the tests may run in parallel.
#
# A BOOT OF ITS OWN, which cmd/README.md SS9 says to take only for something the existing
# tests cannot show.  This is that: /usr/bin/cc searches /usr/bin, /usr/include and /lib by
# ABSOLUTE PATH, and under b6sim those three are the build machine's -- so the b6sim suite
# in cmd/cc/rootfs/test has to pin the sub-tools with B6CPP/B6AS/B6LD, and pinning them is
# what leaves the search itself, and the whole of /lib and /usr/include, unasserted.  No
# other test here boots with a toolchain in mind, and none of them execs a program the
# machine built during the run.
#
# THE FIXTURE IS GENERATED HERE, by the HOST b6cc, and it has to be: the one step of the
# chain the machine cannot take is the first, b6parse/b6lower/b6codegen being the external
# c-compiler's.  So `b6cc -S' turns ../../lib/test/hello.c into the assembly the guest then
# assembles, links and runs -- the same hello.c the libc suite uses, so a failure here is a
# failure of the toolchain and not of an unfamiliar program.  It is NOT checked in: a golden
# .s would freeze the code generator at whatever it was the day it was written, and this
# test would quietly stop noticing that the machine's as and ld still agree with it.
#
# NOTHING IS MASKED, AND NOTHING ABOUT THE LAYOUT IS CHECKED IN.  Every line of the log is
# an exit status, a diagnostic or a count; no segment size and no address appears, because
# every one of those is a property of crt0.o and libc.a rather than of the fixture, and a
# checked-in one would turn an edit to the C library into a failure of this test.  The bytes
# are asserted instead by the LIVE COMPARISON at the foot of this script -- the a.out the
# machine built against the one the host b6cc builds from the same hello.s -- which is the
# closing claim of C9 and needs no expectation file at all.  It is the same argument
# cmd/ar/rootfs/test/run-ar-test.sh makes about golden archives.
set -e
b6fsutil=$1
besm6=$2
b6cc=$3
libcdir=$4
rtdir=$5
srcdir=$6

# The host link below runs in a subdirectory, so every path handed to it has to be absolute.
# ctest passes them that way already; this is for a run by hand.
for _v in b6cc libcdir rtdir; do
    eval "_p=\$$_v"
    case $_p in /*) ;; *) eval "$_v=\$(pwd)/\$_p" ;; esac
done

rm -rf toolchain.img root3102.disk toolchainafter.img toolchain.out toolchain.log \
       toolchain.host toolchain0.drum toolchain1.drum

# THE HOST SIDE WORKS IN A DIRECTORY OF ITS OWN, and the fixture is called hello.s in it --
# the same relative name the guest will type.  That is load-bearing rather than tidy: b6as
# records the SOURCE FILE NAME in the object's symbol table, so two links of the same
# assembly under two different names differ in their string tables and in the header field
# that sizes them.  The comparison at the foot of this script is over the whole file, and
# the file name is an input to it like any other.
mkdir toolchain.host
"$b6cc" -S "$srcdir/../../lib/test/hello.c" -o toolchain.host/hello.s

cp root.img toolchain.img
"$b6fsutil" -a toolchain.img /etc/toolchain "$srcdir/toolchain.sh"
"$b6fsutil" -a toolchain.img /tmp/hello.s toolchain.host/hello.s

"$b6fsutil" -S --volume=3102 toolchain.img root3102.disk

"$besm6" "$srcdir/toolchain.ini"

# What reached the disk.  The script writes and then removes everything it made, so the
# five-pass check is here for the allocation and the free list rather than for a tree: an
# a.out written, executed, relinked and unlinked is a fair amount of block traffic.
"$b6fsutil" -S root3102.disk toolchainafter.img
"$b6fsutil" -c toolchainafter.img

mkdir toolchain.out
"$b6fsutil" -x toolchainafter.img toolchain.out

diff -u "$srcdir/toolchain.expected" toolchain.out/tmp/toolchain.log

# THE CLOSING CLAIM OF TASK C9, and the one assertion here with nothing checked in: the
# a.out the machine linked with its own /usr/bin/{as,ld} against its own /lib is byte for
# byte the one the host build produces from the same assembly.
#
# THE HOST SIDE NAMES THE BUILD TREE'S LIBRARY BY HAND, with -nostdlib, and that is not
# cosmetic: b6cc's own search would find the INSTALLED share/besm6/lib, which is whatever
# `make install' last put there and is routinely a different build from the build/lib/libc
# that was staged onto the image half a minute ago.  Comparing against it would make this
# test fail on how recently somebody installed rather than on whether the two linkers agree
# -- the same reason cmd/ar/rootfs/test insists on the freshly built host tool.  The guest
# side names nothing at all: it found /lib by itself, which is the other half of the claim.
(cd toolchain.host && "$b6cc" -nostdlib -o hello "$libcdir/crt0.o" hello.s \
                             -L "$libcdir" -L "$rtdir" -lc -lruntime)
cmp toolchain.host/hello toolchain.out/tmp/hello
