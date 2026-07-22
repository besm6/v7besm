# Standalone BESM-6 programs, run on the SIMH simulator (`besm6').
#
# These are not host unit tests -- they are the only way to run kernel code on the
# target machine while kernel/besm6.S:_start is still a stub.  The C programs link
# against the crt0.s here rather than the libc one; see the comment in crt0.s for
# why (the libc crt0 exits through a syscall extracode, which needs a kernel).

CC      = b6cc
AS      = b6as
LD      = b6ld
DISASM  = b6disasm
NM      = b6nm

CFLAGS  = -I../../include
LIBDIR  = $$HOME/.local/share/besm6/lib

# b6cc has no -M/-MD (cmd/cc rejects them outright), so header dependencies here are
# necessarily coarse: touching any system header rebuilds every object in this directory.
# That is forty-odd small compiles, which is the right trade for a test directory -- the
# kernel Makefile next door carries a hand-maintained per-file block instead.
HDRS    = $(wildcard ../../include/sys/*.h)

# Scoped by suffix, and deliberately NOT `VPATH = ..:../dev'.  A blanket VPATH searches
# for every kind of prerequisite, including .o files: with kernel/ built, make would find
# ../utab.o, judge it up to date and link the kernel tree's object -- assembled by a
# different tool -- instead of building one here, and `make clean' would not clear it.
# Worse, `..' contains a directory named `test', so the phony `test' target would resolve
# to it and `make test' would report "up to date" without running anything.
vpath %.c .. ../dev
vpath %.s ..
vpath %.S ..

# The kernel C sources built for this directory.  Only these get -DKERNEL; the local test
# programs must not have it, and fall through to the plain %.o: %.c rule below.
KERNOBJ = bio.o mb.o md.o slp.o malloc.o syscall.o clock.o intr.o utab.o

PROGS   = hello sctest mmutest uintr utrap usys uclock uswtch usched ugrow biotest ugrp mbtest \
	  mdtest

.PHONY: all test clean

all:    $(PROGS)

# The link and its postlude, shared by every program but `hello'.  $^ is the prerequisite
# list, so each rule names its objects once instead of twice.  Never give a link rule an
# ordinary non-object prerequisite -- $^ would hand it to b6ld; use an order-only one
# (`| foo'), which $^ excludes.
# -lruntime is the b$* helper archive and the only library any of these link; a test
# program is kernel code with a crt0.s of its own, so a user-mode libc is as wrong
# here as it is next door in kernel/Makefile.
define link
$(LD) $^ -o $@ -L$(LIBDIR) -lruntime
$(DISASM) -c $@ > $@.dis
$(NM) -n $@ > $@.nm
endef

# Run everything under SIMH.  Each .ini asserts on the halt PC and the accumulator
# and exits non-zero on failure, so a clean `make test' means the console worked.
# One loop rather than a second copy of the name list; `set -e' keeps it fail-fast.
test:   $(PROGS)
	@set -e; for p in $(PROGS); do \
		case $$p in \
		mdtest) rm -f mdshort2056.disk; : > mdshort2056.disk;; \
		esac; \
		echo "besm6 $$p.ini"; \
		besm6 $$p.ini; \
	done

clean:
	rm -f *.o *.dis *.nm *.i *.drum *.disk $(PROGS)

# hello is the odd one out: it calls into no libc, and its .nm is left in symbol order
# rather than address order.
hello:  hello.o
	$(LD) $^ -o $@
	$(DISASM) -c $@ > $@.dis
	$(NM) $@ > $@.nm

sctest: crt0.o sctest.o
	$(link)

# mmutest links the kernel's real utab.o, brz.o, uarea.o, seg.o and usermem.o -- besm6.o
# cannot come along (its 0500 vector reaches into the C kernel, and its _start seeds no
# stack), so the five are compiled here from the kernel sources next door.
mmutest: crt0.o mmutest.o mmuhelp.o utab.o brz.o uarea.o seg.o usermem.o
	$(link)

# uintr forges a user-mode context (crt0u.o) and takes an external interrupt in it, linking the
# kernel's real utab.o (sureg/physaddr) and brz.o (drainbrz) -- the from-user counterpart of
# sctest, which enters intrgate from kernel mode.  See uintr.ini and crt0u.S.
uintr:  crt0u.o uintr.o utab.o brz.o
	$(link)

# utrap is uintr's counterpart for the OTHER asynchronous door: it forges a user-mode context
# (crt0t.o), takes data-protection faults in it, and checks that the 0500 gate framed the machine
# and that trap() backed the PC up so the faulting instruction re-executed.  The stub trap() reads
# PSW back with __besm6_getpsw() to check the gate opened the interrupt level -- an intrinsic, so
# it needs no object of its own.  See utrap.ini and crt0t.S.
utrap:  crt0t.o utrap.o utab.o brz.o
	$(link)

# usys is the third forge test, on the EXTRACODE door (0577/0550).  Unlike uintr and utrap it
# links the code under test itself -- the kernel's real syscall.o -- which is why that dispatcher
# lives in its own file (kernel/syscall.c); only the gates are duplicated, into crt0s.S.  It also
# needs usermem.o for fuword(), which syscall() marshals the arguments with.  The stubs check the
# gate opened the interrupt level by reading PSW back with __besm6_getpsw(), an intrinsic, so that
# costs no object.  See usys.ini.
usys:   crt0s.o usys.o syscall.o utab.o brz.o usermem.o
	$(link)

# uclock is the fourth forge test: it takes a real TIMER interrupt in user mode and checks that it
# reached clock().  Like usys it links the code under test rather than a copy -- the kernel's own
# intr.o (the ГРП dispatch) and clock.o -- and hand-builds only the environment they name; only the
# gate is duplicated, into crt0c.S.  See uclock.ini.
uclock: crt0c.o uclock.o clock.o intr.o utab.o brz.o
	$(link)

# uswtch -- save()/resume()/idle() and the u-area invariant (task 16).  Links the real
# switch.o, uarea.o, brz.o and intr.o; only clock() and scintr() are stubbed, in
# uswtch.c.  crt0w.S puts `u' at the real 076000 and runs main() on the u-page stack.
uswtch: crt0w.o uswtch.o switch.o uarea.o brz.o intr.o
	$(link)

# usched -- newproc() + swtch() alternate between two processes (task 16, "done when").
# Links the REAL scheduler, slp.o, and lets it drive the switch.  swap/xswap are stubbed in
# usched.c and must never run: there is no disk yet (task 18).
usched: crt0w.o usched.o slp.o switch.o uarea.o brz.o intr.o utab.o seg.o malloc.o
	$(link)

# ugrow is task 17's: it forges user mode with a ONE-page stack at USTKPAGE, stores past the
# stack top, and checks that growing the stack appends a page WITHOUT moving the pages already
# in it -- the geometry the new grow() (kernel/sig.c) relies on to have no copyseg shuffle.
# grow() itself cannot come along (it calls expand(), i.e. the whole swapper), so ugrow.c does
# its mapping half; seg.o is here for clearseg().  See ugrow.ini and crt0g.S.
ugrow:  crt0g.o ugrow.o utab.o brz.o seg.o
	$(link)

# biotest -- task 18a's addressing plumbing.  Links the REAL bio.o (swap() and physio()
# both live there) against stubs for everything else it names, plus utab.o for the
# physaddr/useracc/physrange the translation rests on.  See biotest.ini.
biotest: crt0.o biotest.o bio.o utab.o brz.o
	$(link)

# ugrp -- task 18b.2: МГРП is dynamic, and a WIRED ГРП bit does not wedge extintr().  Links
# the real intr.o and calls extintr() as an ordinary C function, with delivery blocked --
# no gate and no forged context, so crt0.s is enough.  See ugrp.ini.
ugrp:   crt0.o ugrp.o intr.o
	$(link)

# mbtest -- task 18b.3: the drum driver actually moves data.  Links the REAL mb.o and the
# real intr.o (which is what dispatches the drum's ГРП bits to mbintr()), and stubs only
# iodone(), physio() and the two handlers intr.c names.  `u' is plain bss in mbtest.c, as
# in biotest: mbopen() is the only thing in the driver that touches it.  Needs drums
# attached, so see mbtest.ini.
mbtest: crt0.o mbtest.o mb.o intr.o
	$(link)

# mdtest -- task 18b.4: the disk driver, and its two-step protocol; and task 18b.5: telling
# its failures apart through the status register.  The mirror of mbtest: links the REAL md.o
# and intr.o and stubs mbintr() instead of mdintr().  Needs disk units attached, and their
# filenames must carry a volume number in 2048..4095 -- see mdtest.ini.
#
# `make test' truncates mdshort2056.disk to nothing before running it.  Run 4 attaches that
# file WITHOUT `-n' so it stays empty, which is the only way to make this simulator produce a
# soft error -- a read that runs off the end of the backing file -- and so the only way the
# driver's retry path is exercised rather than merely written.  It must be empty on every
# run: run 4 fails a read against it, and a stale formatted file would silently pass.
mdtest: crt0.o mdtest.o md.o intr.o
	$(link)

# A static pattern rule, so -DKERNEL reaches exactly the kernel sources and nothing else.
$(KERNOBJ): %.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

%.o:    %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# The .S sources are preprocessed (the crt0*.S here #include sys/besm6dev.h, and crt0w.S
# also sys/param.h; the kernel's uarea.S, seg.S and usermem.S #include sys/param.h), so
# build them through the compiler driver with the include path, not the bare assembler --
# $(AS) here is b6as itself and passes no -I.  None of them get -DKERNEL, matching the
# hand-written rules this replaced.
#
# A %.o: %.S pattern is safe on this case-insensitive filesystem: make answers "does
# foo.s exist?" from a readdir() cache that is compared case-sensitively on Darwin, not
# from stat(), so the built-in `%.o: %.s' cannot match a .S file.  It can only win if a
# rename has left a stale lowercase directory entry behind -- fix the entry, not this rule.
%.o:    %.S $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# ../brz.s, ../switch.s and the local crt0.s, hello.s and mmuhelp.s have no
# #includes, so no header prerequisite and no cpp.
%.o:    %.s
	$(AS) $< -o $@
