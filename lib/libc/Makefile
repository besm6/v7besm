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
SYS     = exit.o write.o        # phase 1: cerror, errno, the generated stubs
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

libc.a: $(OBJ)
	$(AR) cr $@ $^
	$(RANLIB) $@

clean:
	rm -f *.o *.a

###
# Hand-maintained header dependencies, in the v7 style: b6cc and b6cpp implement no
# -M/-MD family, so nothing regenerates this block.  Empty for now -- everything
# here is assembly, and no .s in this library has an #include.
