# Test programs for the user-level libraries, run under b6sim.
#
# b6sim is the harness until a root filesystem exists: it runs one user a.out and
# services the v7 syscalls on the host, which is what libc needs and is far faster
# than booting SIMH.  Each program is linked against the real crt0.o and libc.a,
# run, and its output diffed against the checked-in .expected file; a program that
# needs arguments lists them in a matching .args file.  Once kernel task 18b.6 lands
# a root filesystem, the same programs go on it and run under the real kernel.

TOP     = ../..

PROGS   = hello vararg errno procs sbrkt malloct strings gen environ jmp headers \
          stdiot printft scanft execs spawn timet pwent signals matht

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

malloct: malloct.o $(LIBDEP)
	$(link)

strings: strings.o $(LIBDEP)
	$(link)

gen:    gen.o $(LIBDEP)
	$(link)

environ: environ.o $(LIBDEP)
	$(link)

jmp:    jmp.o $(LIBDEP)
	$(link)

headers: headers.o $(LIBDEP)
	$(link)

stdiot: stdiot.o $(LIBDEP)
	$(link)

printft: printft.o $(LIBDEP)
	$(link)

scanft: scanft.o $(LIBDEP)
	$(link)

# execs EXECS ITSELF, five times over, which is why it needs no helper program and no
# .args: it passes the stage name in argv[1] and reads it back out of its own argv.
execs:  execs.o $(LIBDEP)
	$(link)

spawn:  spawn.o $(LIBDEP)
	$(link)

timet:  timet.o $(LIBDEP)
	$(link)

pwent:  pwent.o $(LIBDEP)
	$(link)

# signals takes a second of wall clock: sleep(1) is the one path here that leaves a
# handler by longjmp and never returns through the signal frame at all.
signals: signals.o $(LIBDEP)
	$(link)

# matht is the one program here that links libm.  MLIB names it, and $(LIBMDEP) is an
# ordinary prerequisite so a rebuilt libm.a relinks it; $(link) filters both back out.
matht:  MLIB = -L$(LIBM) -lm
matht:  matht.o $(LIBDEP) $(LIBMDEP)
	$(link)

# Run each program and diff its output.  `set -e' keeps it fail-fast, and the run
# itself must exit 0 -- a test reports failure by its status as well as its output.
#
# stderr is captured with stdout: perror writes to fd 2 and nothing else may write
# there, so anything b6sim itself says lands in the diff rather than scrolling past.
test:   $(PROGS)
	@set -e; for p in $(PROGS); do \
		args=`cat $$p.args 2>/dev/null || true`; \
		echo "b6sim ./$$p $$args"; \
		$(SIM) ./$$p $$args > $$p.out 2>&1; \
		diff -u $$p.expected $$p.out; \
	done
	@echo "all tests passed"

clean:
	rm -f *.o *.nm *.dis *.out *.tmp $(PROGS)

###
# Hand-maintained header dependencies; b6cc has no -M.  Most of these include nothing:
# v7 has no <unistd.h> and stdio is phase 4, so each declares the syscalls it calls.

# <stdarg.h>, which varargs.h is a shim over, is the compiler's and is not named here.
vararg.o:	$(TOP)/include/varargs.h
errno.o:	$(TOP)/include/errno.h
gen.o:		$(TOP)/include/ctype.h $(TOP)/include/stdlib.h
jmp.o:		$(TOP)/include/setjmp.h
malloct.o:	$(TOP)/include/stdlib.h
gen.o jmp.o strings.o environ.o malloct.o: $(TOP)/include/string.h

stdiot.o printft.o scanft.o: $(TOP)/include/stdio.h
stdiot.o scanft.o: $(TOP)/include/string.h
printft.o scanft.o: $(TOP)/include/stdlib.h
scanft.o:	$(TOP)/include/math.h

# Phase 5.  execs, spawn and pwent declare the routines they exercise themselves --
# exec*, ttyname, ttyslot, getlogin, getpw, crypt and timezone are named by no header,
# which is lib/README.md's rule for a routine v7 never declared either.
execs.o spawn.o timet.o pwent.o: $(TOP)/include/stdio.h
execs.o timet.o pwent.o: $(TOP)/include/string.h
execs.o spawn.o: $(TOP)/include/stdlib.h
execs.o:	$(TOP)/include/errno.h

# Phase 6.  alarm, pause and getpid are declared here, as everywhere else in this
# directory; signal, raise and sleep come from <signal.h>.
signals.o:	$(TOP)/include/signal.h $(TOP)/include/setjmp.h \
		$(TOP)/include/stdio.h $(TOP)/include/errno.h
timet.o:	$(TOP)/include/time.h
pwent.o:	$(TOP)/include/pwd.h $(TOP)/include/grp.h

# headers.o includes the whole tree, twice, which is the point of it -- so it
# depends on the whole tree, and there is no shorter way to say that here.
headers.o:	$(wildcard $(TOP)/include/*.h) $(wildcard $(TOP)/include/sys/*.h)

# Phase 7.  matht drives libm; <math.h> is ours and <errno.h> gives it EDOM/ERANGE.
matht.o:	$(TOP)/include/math.h $(TOP)/include/errno.h $(TOP)/include/stdio.h
