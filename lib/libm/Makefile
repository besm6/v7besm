# libm for programs running under this kernel.  See ../README.md.
#
# The v7 math library refitted to the BESM-6's one 48-bit floating format: a 40-bit
# mantissa and the 2^+-63 exponent, no infinities, NaNs or denormals.  One product,
# libm.a; a program that uses it links -lm, which b6cc places ahead of the implicit
# -lc (libm calls errno, frexp, ldexp and modf; libc calls nothing back).
#
# Built by the INSTALLED toolchain, like libc -- so `make && make install' at the top
# level must come first, then `make -C lib' builds this, then `make -C lib install'.
# NOT part of the CMake build, for the same reason libc is not.

TOP     = ../..

# One function to a file where the routine stands alone; the v7 grouping kept where a
# set of entry points shares file-scope statics -- the coefficient tables, or j0.c's
# pzero/qzero -- which is the exception malloc.c and ctime.c already take in libc.
#
# fmod.o and fma.o have no v7 ancestor: v7 had no fmod (the file that reached this
# tree was an fdlibm import for IEEE, useless here) and fma is C11.  trunc, round,
# copysign, fmin and fmax are C11 too and are a few lines each over modf/comparison.
OBJ     = fabs.o floor.o trunc.o round.o copysign.o fmin.o fmax.o fma.o fmod.o \
          sqrt.o hypot.o pow.o exp.o log.o \
          sin.o tan.o asin.o atan.o sinh.o tanh.o \
          erf.o j0.o j1.o jn.o

.PHONY: all install clean

all:    libm.a

include $(TOP)/lib/rules.mk

libm.a: $(OBJ)
	$(AR) cr $@ $^
	$(RANLIB) $@

# Install beside libc.a and libruntime.a in share/besm6/lib, where b6cc's -lm finds
# it.  cp -p for the reason libc/Makefile gives: b6ld calls a randomized archive out
# of date when its mtime is newer than the date in __.SYMDEF, and a plain cp would
# stamp the copy with the time of the copy.  PREFIX and LIBDIR come from rules.mk.
install: all
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp -p libm.a $(DESTDIR)$(LIBDIR)

clean:
	rm -f *.o *.a

###
# Hand-maintained header dependencies, in the v7 style: b6cc and b6cpp implement no
# -M/-MD family, so nothing regenerates this block.  <math.h> is ours and carries
# HUGE_VAL and the M_* constants; the <float.h> it pulls in is the COMPILER's and is
# outside this tree, so it is not named -- the same rule libc/Makefile follows.  The
# routines with a domain or range error also name <errno.h>.
$(OBJ):		$(TOP)/include/math.h
sqrt.o hypot.o pow.o exp.o log.o tan.o asin.o fmod.o \
erf.o j0.o j1.o jn.o:	$(TOP)/include/errno.h
