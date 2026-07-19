# Standalone BESM-6 programs, run on the SIMH simulator (`besm6`).
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

PROGS   = hello sctest mmutest uintr utrap usys uclock uswtch usched ugrow biotest ugrp

all:    $(PROGS)

# Run everything under SIMH.  Each .ini asserts on the halt PC and the accumulator
# and exits non-zero on failure, so a clean `make test` means the console worked.
test:   $(PROGS)
	besm6 hello.ini
	besm6 sctest.ini
	besm6 mmutest.ini
	besm6 uintr.ini
	besm6 utrap.ini
	besm6 usys.ini
	besm6 uclock.ini
	besm6 uswtch.ini
	besm6 usched.ini
	besm6 ugrow.ini
	besm6 biotest.ini
	besm6 ugrp.ini

clean:
	rm -f *.o *.dis *.nm $(PROGS)

hello:  hello.o
	$(LD) $< -o $@
	$(DISASM) -c $@ > $@.dis
	$(NM) $@ > $@.nm

sctest: crt0.o sctest.o
	$(LD) crt0.o sctest.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# mmutest links the kernel's real utab.o, brz.o, uarea.o, seg.o and usermem.o -- besm6.o
# cannot come along (its 0500 vector reaches into the C kernel, and its _start seeds no
# stack), so the five are compiled here from the kernel sources next door.
mmutest: crt0.o mmutest.o mmuhelp.o utab.o brz.o uarea.o seg.o usermem.o
	$(LD) crt0.o mmutest.o mmuhelp.o utab.o brz.o uarea.o seg.o usermem.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# uintr forges a user-mode context (crt0u.o) and takes an external interrupt in it, linking the
# kernel's real utab.o (sureg/physaddr) and brz.o (drainbrz) -- the from-user counterpart of
# sctest, which enters intrgate from kernel mode.  See uintr.ini and crt0u.S.
uintr:  crt0u.o uintr.o utab.o brz.o
	$(LD) crt0u.o uintr.o utab.o brz.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# utrap is uintr's counterpart for the OTHER asynchronous door: it forges a user-mode context
# (crt0t.o), takes data-protection faults in it, and checks that the 0500 gate framed the machine
# and that trap() backed the PC up so the faulting instruction re-executed.  See utrap.ini and
# crt0t.S.
utrap:  crt0t.o utrap.o utab.o brz.o
	$(LD) crt0t.o utrap.o utab.o brz.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# usys is the third forge test, on the EXTRACODE door (0577/0550).  Unlike uintr and utrap it
# links the code under test itself -- the kernel's real syscall.o -- which is why that dispatcher
# lives in its own file (kernel/syscall.c); only the gates are duplicated, into crt0s.S.  It also
# needs usermem.o for fuword(), which syscall() marshals the arguments with.  See usys.ini.
usys:   crt0s.o usys.o syscall.o utab.o brz.o usermem.o
	$(LD) crt0s.o usys.o syscall.o utab.o brz.o usermem.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# uclock is the fourth forge test: it takes a real TIMER interrupt in user mode and checks that it
# reached clock().  Like usys it links the code under test rather than a copy -- the kernel's own
# intr.o (the ГРП dispatch) and clock.o -- and hand-builds only the environment they name; only the
# gate is duplicated, into crt0c.S.  See uclock.ini.
uclock: crt0c.o uclock.o clock.o intr.o utab.o brz.o psw.o
	$(LD) crt0c.o uclock.o clock.o intr.o utab.o brz.o psw.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# uswtch -- save()/resume()/idle() and the u-area invariant (task 16).  Links the real
# switch.o, uarea.o, brz.o, intr.o and psw.o; only clock() and scintr() are stubbed, in
# uswtch.c.  crt0w.s puts `u' at the real 076000 and runs main() on the u-page stack.
uswtch: crt0w.o uswtch.o switch.o uarea.o brz.o intr.o psw.o
	$(LD) crt0w.o uswtch.o switch.o uarea.o brz.o intr.o psw.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# usched -- newproc() + swtch() alternate between two processes (task 16, "done when").
# Links the REAL scheduler, slp.o, and lets it drive the switch.  swap/xswap are stubbed in
# usched.c and must never run: there is no disk yet (task 18).
usched: crt0w.o usched.o slp.o switch.o uarea.o brz.o intr.o psw.o utab.o seg.o malloc.o
	$(LD) crt0w.o usched.o slp.o switch.o uarea.o brz.o intr.o psw.o utab.o seg.o malloc.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# ugrow is task 17's: it forges user mode with a ONE-page stack at USTKPAGE, stores past the
# stack top, and checks that growing the stack appends a page WITHOUT moving the pages already
# in it -- the geometry the new grow() (kernel/sig.c) relies on to have no copyseg shuffle.
# grow() itself cannot come along (it calls expand(), i.e. the whole swapper), so ugrow.c does
# its mapping half; seg.o is here for clearseg().  See ugrow.ini and crt0g.S.
ugrow:  crt0g.o ugrow.o utab.o brz.o seg.o
	$(LD) crt0g.o ugrow.o utab.o brz.o seg.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# biotest -- task 18a's addressing plumbing.  Links the REAL bio.o (swap() and physio()
# both live there) against stubs for everything else it names, plus utab.o for the
# physaddr/useracc/physrange the translation rests on.  See biotest.ini.
biotest: crt0.o biotest.o bio.o utab.o brz.o
	$(LD) crt0.o biotest.o bio.o utab.o brz.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

# ugrp -- task 18b.2: МГРП is dynamic, and a WIRED ГРП bit does not wedge extintr().  Links
# the real intr.o and calls extintr() as an ordinary C function, with delivery blocked --
# no gate and no forged context, so crt0.s is enough.  See ugrp.ini.
ugrp:   crt0.o ugrp.o intr.o psw.o
	$(LD) crt0.o ugrp.o intr.o psw.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

bio.o: ../dev/bio.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

slp.o: ../slp.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

malloc.o: ../malloc.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

switch.o: ../switch.s
	$(AS) $< -o $@

syscall.o: ../syscall.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

clock.o: ../clock.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

intr.o: ../intr.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

# crt0u.S/crt0t.S/crt0s.S/crt0c.S are preprocessed (they #include sys/besm6dev.h), so build them through the
# compiler driver with the include path, not the bare assembler.
crt0u.o: crt0u.S
	$(CC) $(CFLAGS) -c $< -o $@

crt0t.o: crt0t.S
	$(CC) $(CFLAGS) -c $< -o $@

crt0s.o: crt0s.S
	$(CC) $(CFLAGS) -c $< -o $@

crt0c.o: crt0c.S
	$(CC) $(CFLAGS) -c $< -o $@

crt0g.o: crt0g.S
	$(CC) $(CFLAGS) -c $< -o $@

utab.o: ../utab.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

brz.o:  ../brz.s
	$(AS) $< -o $@

uarea.o: ../uarea.s
	$(AS) $< -o $@

seg.o:  ../seg.s
	$(AS) $< -o $@

usermem.o: ../usermem.s
	$(AS) $< -o $@

# cli/sti: setipl() in intr.c is built on them (БлПр is the interrupt priority).
psw.o:  ../psw.s
	$(AS) $< -o $@

%.o:    %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o:    %.s
	$(AS) $< -o $@
