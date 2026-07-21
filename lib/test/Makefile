# Test programs for the user-level libraries, run under b6sim.
#
# b6sim is the harness until a root filesystem exists: it runs one user a.out and
# services the v7 syscalls on the host, which is what libc needs and is far faster
# than booting SIMH.  Each program is linked against the real crt0.o and libc.a,
# run, and its output diffed against the checked-in .expected file; a program that
# needs arguments lists them in a matching .args file.  Once kernel task 18b.6 lands
# a root filesystem, the same programs go on it and run under the real kernel.

TOP     = ../..

PROGS   = hello vararg errno procs sbrkt

.PHONY: all test clean

all:    $(PROGS)

include $(TOP)/lib/rules.mk

# $(LIBDEP) is crt0.o and libc.a, named as ORDINARY prerequisites so that rebuilding
# the library relinks every program; $(link) filters them back out of $^ before calling
# b6ld, which is what keeps them from appearing twice on the link line.  See rules.mk.
hello:  hello.o $(LIBDEP)
	$(link)

vararg: vararg.o $(LIBDEP)
	$(link)

errno:  errno.o $(LIBDEP)
	$(link)

procs:  procs.o $(LIBDEP)
	$(link)

sbrkt:  sbrkt.o $(LIBDEP)
	$(link)

# Run each program and diff its output.  `set -e' keeps it fail-fast, and the run
# itself must exit 0 -- a test reports failure by its status as well as its output.
test:   $(PROGS)
	@set -e; for p in $(PROGS); do \
		args=`cat $$p.args 2>/dev/null || true`; \
		echo "b6sim ./$$p $$args"; \
		$(SIM) ./$$p $$args > $$p.out; \
		diff -u $$p.expected $$p.out; \
	done
	@echo "all tests passed"

clean:
	rm -f *.o *.nm *.dis *.out $(PROGS)

###
# Hand-maintained header dependencies; b6cc has no -M.  Most of these include nothing:
# v7 has no <unistd.h> and stdio is phase 4, so each declares the syscalls it calls.

vararg.o:	$(TOP)/include/varargs.h
errno.o:	$(TOP)/include/errno.h
