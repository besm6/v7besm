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
# The strings, the ctype table and the small utilities.  Where the c-compiler's own
# library already has a BESM-6-aware ANSI version, it was taken rather than ported --
# the whole str*/mem* family and atoi -- and each such file says so at its head; the
# rest are ports of the v7 sources.  setjmp.o is neither: v7's is x86 assembly, so it
# is written here from the calling convention.
GEN     = strcat.o strcmp.o strcpy.o strlen.o strncat.o strncmp.o strncpy.o \
          strchr.o strrchr.o strstr.o strtok.o \
          memcpy.o memmove.o memset.o memcmp.o memchr.o \
          abs.o atoi.o atol.o index.o rindex.o swab.o qsort.o rand.o \
          getenv.o mktemp.o isatty.o sleep.o perror.o errlst.o ctype_.o abort.o \
          setjmp.o assert.o toupper.o tolower.o isblank.o
STDIO   =                       # phase 4

OBJ     = $(SYS) $(GEN) $(STDIO)

.PHONY: all install clean

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

# Install beside the c-compiler's libruntime.a, which is where b6cc looks for both:
# find_crt0() names crt0.o outright and add_default_libdirs() puts a -L on the same
# directory for the implicit -lc (cmd/cc/cc.c).  PREFIX and LIBDIR come from rules.mk.
#
# NOT a CMake install, and it cannot be one: lib/ is built by the INSTALLED toolchain,
# so b6cc has to exist before libc.a can be compiled at all.  Hence the three-step
# bootstrap of a fresh checkout -- `make && make install', `make -C lib',
# `make -C lib install' -- which the top-level README.md spells out.
#
# cp -p, and it matters: b6ld calls a randomized archive out of date when the
# file's mtime is newer than the date stored in its __.SYMDEF member
# (cmd/ld/library.c), and a plain cp stamps the copy with the time of the copy.
# Preserving the times keeps b6ranlib's index trustworthy where it lands.
install: all
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp -p libc.a $(CSU) $(DESTDIR)$(LIBDIR)

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

# gen/.  <string.h> and <stdlib.h> live in $(TOP)/include and change with us, so they
# are named.  The <stddef.h> they pull in does not: it is the COMPILER's, installed into
# share/besm6/include by the c-compiler and outside this tree -- the same reason no
# <stdarg.h> appears in this block either.
strcat.o strcmp.o strcpy.o strlen.o strncat.o strncmp.o strncpy.o strchr.o \
strrchr.o strstr.o strtok.o memcpy.o memmove.o memset.o memcmp.o memchr.o \
perror.o assert.o:	$(TOP)/include/string.h
abs.o atoi.o atol.o qsort.o rand.o: $(TOP)/include/stdlib.h

ctype_.o toupper.o tolower.o isblank.o: $(TOP)/include/ctype.h
perror.o errlst.o: $(TOP)/include/errno.h
isatty.o:	$(TOP)/include/sgtty.h
abort.o:	$(TOP)/include/signal.h
sleep.o:	$(TOP)/include/signal.h $(TOP)/include/setjmp.h
assert.o:	$(TOP)/include/assert.h
