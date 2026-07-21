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

# The v7 headers win over the compiler's own: cmd/cc appends share/besm6/include
# AFTER the user's -I, so stdio.h, ctype.h and errno.h come from include/ here
# while <stdarg.h> still resolves.  See lib/README.md, "Ground rules".
CFLAGS  = -I$(TOP)/include

# Our library: libc.a and, beside it rather than in it, crt0.o.
LIBC    = $(TOP)/lib/libc

# The external c-compiler's library, reached ONLY for the b$* compiler-support
# helpers (b$save, b$ret, b$mul, ...) that every compiled function calls.
BLIB    = $(HOME)/.local/share/besm6/lib

# The link, and its postlude.  $^ is the prerequisite list, so a program names its
# objects once; crt0.o and the two archives are therefore NOT prerequisites of the
# rule -- name them order-only (`| $(LIBC)/crt0.o $(LIBC)/libc.a') or $^ would hand
# them to b6ld a second time.
#
# The external library is named by PATH, not by `-L$(BLIB) -lc'.  b6ld keeps ONE
# global list of -L directories and `-l' searches all of it, first match wins
# (cmd/ld/pass1.c, cmd/ld/library.c), so with both archives called libc.a a second
# `-lc' finds ours again and the b$* helpers never resolve.  A bare archive path is
# scanned as a library just the same (open_input recognizes it by its ARMAG).
define link
$(LD) $(LIBC)/crt0.o $^ -o $@ -L$(LIBC) -lc $(BLIB)/libc.a
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
