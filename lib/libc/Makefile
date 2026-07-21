# libc for programs running under this kernel.  See ../README.md.
#
# Two products: the archive libc.a, and crt0.o beside it rather than in it -- a
# program's startup is named on the link line, never pulled by symbol, which is
# where v7 keeps it too (/lib/crt0.o).
#
# The source directories are the v7 ones (csu, sys, gen, stdio); the objects all
# land here, flat, as the kernel's do for dev/.  One archive with a b6ranlib index,
# so a member calling back into another resolves in a single scan.

TOP     = ../..

CSU     = crt0.o                # not archived: see above

# The syscall leaves.  Most of them are GENERATED, one .S and one object per line of
# sys/syscalls.tbl, so that b6ranlib's index lets b6ld pull only what a program calls;
# the generated sources land here, beside the objects, and are cleaned away with them
# (.gitignore has lib/libc/*.S, which does not match the hand-written sys/*.S).
#
# .S, not .s: a leaf names its syscall as the SYS_* macro of <sys/syscall.h>, and b6cc
# dispatches a .S through the preprocessor where a .s goes straight to the assembler
# (../rules.mk).  That is why no number appears anywhere under lib/.
#
# STUBS is read from the table at parse time, so adding a syscall is adding a line
# there and nothing else.  SYS lists only the leaves the generator cannot produce:
# the ones with a second result in r12, an argument that must survive the trap, or no
# return on success.  Each says at its head why it is hand-written.
STUBS   = $(shell sed -e 's/\#.*//' sys/syscalls.tbl | awk 'NF { print $$1 }')
STUBSRC = $(STUBS:%=%.S)
STUBOBJ = $(STUBS:%=%.o)

SYS     = cerror.o exit.o fork.o wait.o pipe.o getpid.o getuid.o getgid.o \
          time.o dup.o exec.o exece.o sbrk.o $(STUBOBJ)
GEN     =                       # phase 2
STDIO   =                       # phase 4

OBJ     = $(SYS) $(GEN) $(STDIO)

.PHONY: all clean

all:    libc.a $(CSU)

include $(TOP)/lib/rules.mk

# Scoped by suffix, and deliberately not a blanket VPATH: that would search for .o
# prerequisites too, and one of the directories below could then supply an object
# instead of having one built here.  See kernel/test/Makefile for the full story.
vpath %.c csu sys gen stdio
vpath %.s csu sys gen stdio
vpath %.S csu sys gen stdio

# A static pattern rule, not an implicit one: the targets are known from the table, and
# naming them keeps make from ever looking for a `%.S' under sys/ for a generated stub.
$(STUBSRC): %.S: sys/syscalls.tbl sys/mkstub
	sh sys/mkstub $* sys/syscalls.tbl > $@

libc.a: $(OBJ)
	$(AR) cr $@ $^
	$(RANLIB) $@

clean:
	rm -f *.o *.a $(STUBSRC)

###
# Hand-maintained header dependencies, in the v7 style: b6cc and b6cpp implement no
# -M/-MD family, so nothing regenerates this block.

# Every leaf that issues an extracode names its number through <sys/syscall.h> -- the
# generated stubs and the hand-written .S ones alike.  cerror.o is the one object in
# SYS that does not: it has no extracode, which is why it is still a .s.
$(STUBOBJ):	$(TOP)/include/sys/syscall.h
exit.o fork.o wait.o pipe.o getpid.o getuid.o getgid.o time.o dup.o exec.o exece.o: \
		$(TOP)/include/sys/syscall.h

sbrk.o:		$(TOP)/include/sys/param.h $(TOP)/include/sys/types.h
