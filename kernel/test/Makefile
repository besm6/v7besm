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

PROGS   = hello sctest mmutest

all:    $(PROGS)

# Run everything under SIMH.  Each .ini asserts on the halt PC and the accumulator
# and exits non-zero on failure, so a clean `make test` means the console worked.
test:   $(PROGS)
	besm6 hello.ini
	besm6 sctest.ini
	besm6 mmutest.ini

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

# mmutest links the kernel's real utab.o, brz.o, uarea.o and seg.o -- besm6.o cannot
# come along (its 0500 vector reaches into the C kernel, and its _start seeds no stack),
# so the four are compiled here from the kernel sources next door.
mmutest: crt0.o mmutest.o mmuhelp.o utab.o brz.o uarea.o seg.o
	$(LD) crt0.o mmutest.o mmuhelp.o utab.o brz.o uarea.o seg.o -o $@ -L$(LIBDIR) -lc
	$(DISASM) -c $@ > $@.dis
	$(NM) -n $@ > $@.nm

utab.o: ../utab.c
	$(CC) $(CFLAGS) -DKERNEL -c $< -o $@

brz.o:  ../brz.s
	$(AS) $< -o $@

uarea.o: ../uarea.s
	$(AS) $< -o $@

seg.o:  ../seg.s
	$(AS) $< -o $@

%.o:    %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o:    %.s
	$(AS) $< -o $@
