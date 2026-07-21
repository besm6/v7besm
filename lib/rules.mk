# Shared definitions for everything under lib/: the tools, the flags, the compile
# rules and -- the reason this file exists -- the link recipe, in one place.
#
# Include it as
#       TOP = ../..
#       all: ...
#       include $(TOP)/lib/rules.mk
# with TOP naming the repository root.  Declare the default target BEFORE the
# include: the pattern rules below cannot become a default goal, but a future
# addition here could, and make takes the first ordinary target in the file it
# reads first.
#
# Not part of the CMake build.  The toolchain must be `make install'ed first --
# these are the installed b6* cross tools, not the host compiler.

CC      = b6cc
AS      = b6as
AR      = b6ar
RANLIB  = b6ranlib
LD      = b6ld
NM      = b6nm
SIZE    = b6size -w
DISASM  = b6disasm
SIM     = b6sim

# One header tree, and it is ours: include/ holds the v7 headers and the C11 ones
# alike.  -I names it in the source tree, ahead of the installed copy that cmd/cc
# appends, so a build here sees what it just edited.  See lib/README.md.
CFLAGS  = -I$(TOP)/include

# Our library: libc.a and, beside it rather than in it, crt0.o.
LIBC    = $(TOP)/lib/libc

# Where `make install' puts them, and where the toolchain looks: the same prefix
# rule the top-level Makefile uses -- ~/.local if it exists, else /usr/local --
# and the same share/besm6/lib the c-compiler installs libruntime.a into.  This
# is also the -L that resolves -lruntime on every link below.
PREFIX  = $(shell [ -d $$HOME/.local ] && echo $$HOME/.local || echo /usr/local)
LIBDIR  = $(PREFIX)/share/besm6/lib

# What every program links against besides its own objects.  Name it as an ORDINARY
# prerequisite (`prog: prog.o $(LIBDEP)') so that rebuilding libc.a relinks every
# program that uses it; the recipe filters it back out of $^ before handing the list
# to b6ld, which is what keeps these two from being named twice on the link line.
# Order-only would read more neatly and be wrong: order-only prerequisites never
# trigger a rebuild, so a program would go on using the libc it was first linked with.
LIBDEP  = $(LIBC)/crt0.o $(LIBC)/libc.a

# The link, and its postlude.  A program names its own objects once, as prerequisites.
#
# Two archives, ours first: libc.a here, then the c-compiler's libruntime.a, which is
# reached ONLY for the b$* compiler-support helpers (b$save, b$ret, b$mul, ...) that
# every compiled function calls.  The order is the contract -- b6ld scans an archive
# once, in order (cmd/ld/library.c), libc calls the helpers and no helper calls back --
# and the two -L directories are searched by both -l's, which is harmless now that the
# names differ.  They did not always: the external library used to be called libc.a too,
# and had to be named by absolute path to keep a second `-lc' from finding ours again.
define link
$(LD) $(LIBC)/crt0.o $(filter-out $(LIBDEP),$^) -o $@ -L$(LIBC) -L$(LIBDIR) -lc -lruntime
$(NM) -n $@ > $@.nm
$(DISASM) -c $@ > $@.dis
$(SIZE) $@
endef

%.o:    %.c
	$(CC) $(CFLAGS) -c $< -o $@

# .s goes straight to the assembler; .S is preprocessed first, so it goes through
# the driver, which is the only one of the two that passes -I.
%.o:    %.s
	$(AS) $< -o $@

%.o:    %.S
	$(CC) $(CFLAGS) -c $< -o $@
