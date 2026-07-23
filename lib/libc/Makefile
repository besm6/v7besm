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

#
# execl.o, execv.o and execle.o are C and not leaves at all: they are the wrappers that
# build an argv[] for the exec() and exece() gates above.  They cost nothing to build,
# because the C calling convention has ALREADY laid the argument list out as the vector
# the gate wants -- see sys/execl.c.  The two that search $PATH are gen/, as in v7.
SYS     = cerror.o exit.o fork.o wait.o pipe.o getpid.o getuid.o getgid.o \
          time.o dup.o exec.o exece.o execl.o execv.o execle.o sbrk.o $(STUBOBJ)
# The strings, the ctype table and the small utilities.  Where the c-compiler's own
# library already has a BESM-6-aware ANSI version, it was taken rather than ported --
# the whole str*/mem* family and atoi -- and each such file says so at its head; the
# rest are ports of the v7 sources.  setjmp.o is neither: v7's is x86 assembly, so it
# is written here from the calling convention.
#
# malloc.o is v7's allocator and NOT the c-compiler's, which would claim the whole span
# up to the stack on first use; it holds free() and realloc() too, since the three share
# the arena statics, the same reason brk/sbrk share one file.  aligned_alloc.o is C11's
# and has no v7 ancestor.
#
# cuexit.o is `exit', which is NOT sys/exit.S -- that one keeps `_exit' alone.  The
# difference between them is the _cleanup() that flushes stdio, and the file carries
# v7's name because `exit.c' would collide with sys/exit.S in this flat object
# directory.
#
# modf.o, frexp.o and ldexp.o are floating-point but not libm: atof() calls ldexp,
# and ecvt() and the printf engine call modf, so the CONVERSIONS need them and a
# program gets them without -lm.  v7's gen/ holds the same three.  modf came from the
# c-compiler's tree, as the str*/mem* family did; frexp and ldexp came from its .s
# sources and each says so at its head.
#
# ctime.o is the whole of v7's date conversion in one file, as v7 kept it -- ctime,
# localtime, gmtime, asctime and dysize share the struct tm and the text buffer they
# hand back.  clock.o, difftime.o, mktime.o and strftime.o had no v7 ancestor and are
# written from C11 §7.27, which <time.h> now declares in full and libc.a answers in
# full.  timezone.o is v7's and is named by no header at all.
#
# crypt.o holds crypt, setkey and encrypt: one DES key schedule, three entry points.
GEN     = strcat.o strcmp.o strcpy.o strlen.o strncat.o strncmp.o strncpy.o \
          strchr.o strrchr.o strstr.o strtok.o \
          memcpy.o memmove.o memset.o memcmp.o memchr.o \
          abs.o atoi.o atol.o index.o rindex.o swab.o qsort.o rand.o \
          getenv.o mktemp.o isatty.o sleep.o perror.o errlst.o ctype_.o abort.o \
          raise.o \
          setjmp.o assert.o toupper.o tolower.o isblank.o \
          malloc.o calloc.o aligned_alloc.o cuexit.o \
          atof.o ecvt.o modf.o frexp.o ldexp.o \
          execvp.o execlp.o ttyname.o ttyslot.o getlogin.o tell.o crypt.o \
          ctime.o clock.o difftime.o mktime.o strftime.o timezone.o
# The FILE machinery, v7's, plus the C11 routines v7 never had -- <stdio.h> declares
# the whole of §7.21 and libc.a answers all of it.
#
# doprnt.o is the exception to "ported from v7": v7's _doprnt was x86 assembly, so
# this one is the c-compiler's doprnt.c retargeted to a FILE * sink.  Everything in
# the printf family reaches it through vfprintf.o, and everything in the sprintf
# family through vsnprintf.o, which builds an _IOSTRG stream over the caller's buffer.
#
# The macro pairs of <stdio.h> need function forms as well (C11 §7.1.4), which is
# what getc.o, putc.o, feof.o and ferror.o are; v7 shipped only getchar and putchar.
#
# The last two rows are stdio/ only because v7 put them there, and v7 put them there
# because they are the routines that read a FILE * out of somewhere other than the
# caller: popen.o holds popen AND pclose, which share the pid table, and the pw/gr
# families each keep one open stream and one line buffer between their three or five
# entry points -- which is why the walkers are one object and the lookups are not.
STDIO   = data.o findiop.o endopen.o fopen.o fdopen.o freopen.o filbuf.o flsbuf.o \
          fseek.o ftell.o rew.o fgetpos.o fsetpos.o \
          setbuf.o setvbuf.o clrerr.o feof.o ferror.o \
          fgetc.o fputc.o getc.o putc.o getchar.o putchar.o ungetc.o \
          fgets.o fputs.o gets.o puts.o fread.o fwrite.o getw.o putw.o \
          remove.o rename.o tmpnam.o tmpfile.o \
          doprnt.o printf.o fprintf.o sprintf.o snprintf.o \
          vprintf.o vfprintf.o vsprintf.o vsnprintf.o \
          doscan.o scanf.o fscanf.o sscanf.o vscanf.o vfscanf.o vsscanf.o \
          gcvt.o \
          system.o popen.o \
          getpwent.o getpwnam.o getpwuid.o getgrent.o getgrnam.o getgrgid.o \
          getpw.o getpass.o

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

# execl.o, execv.o and execle.o name no header of ours at all: <stdarg.h> is the
# COMPILER's, and `environ' and exece() are declared at each file's own head.

# gen/.  <string.h> and <stdlib.h> live in $(TOP)/include and change with us, so they
# are named.  The <stddef.h> they pull in does not: it is the COMPILER's, installed into
# share/besm6/include by the c-compiler and outside this tree -- the same reason no
# <stdarg.h> appears in this block either.
strcat.o strcmp.o strcpy.o strlen.o strncat.o strncmp.o strncpy.o strchr.o \
strrchr.o strstr.o strtok.o memcpy.o memmove.o memset.o memcmp.o memchr.o \
perror.o assert.o:	$(TOP)/include/string.h
abs.o atoi.o atol.o qsort.o rand.o: $(TOP)/include/stdlib.h
malloc.o calloc.o aligned_alloc.o: $(TOP)/include/stdlib.h
calloc.o:	$(TOP)/include/string.h

ctype_.o toupper.o tolower.o isblank.o: $(TOP)/include/ctype.h
perror.o errlst.o: $(TOP)/include/errno.h
isatty.o:	$(TOP)/include/sgtty.h
abort.o raise.o:	$(TOP)/include/signal.h
sleep.o:	$(TOP)/include/signal.h $(TOP)/include/setjmp.h
assert.o:	$(TOP)/include/assert.h
cuexit.o:	$(TOP)/include/stdlib.h

# The conversions.  <math.h> is ours and carries LOGHUGE, which atof() reads; <float.h>,
# which it includes, is the COMPILER's and is outside this tree, so it is not named --
# the same rule as <stddef.h> above.  frexp.o and ldexp.o are assembly and include
# nothing at all.
atof.o ecvt.o modf.o:	$(TOP)/include/math.h
atof.o:		$(TOP)/include/ctype.h $(TOP)/include/stdlib.h

# The date conversions.  <sys/timeb.h> and <sys/times.h> both name time_t and neither
# defines it, so <sys/types.h> comes first in each of these files and is named here.
ctime.o mktime.o strftime.o: $(TOP)/include/sys/types.h $(TOP)/include/sys/timeb.h
clock.o:	$(TOP)/include/sys/types.h $(TOP)/include/sys/times.h
ctime.o clock.o difftime.o mktime.o strftime.o: $(TOP)/include/time.h
timezone.o:	$(TOP)/include/stdio.h

# The rest of phase 5's gen/.  execlp.o, crypt.o and tell.o are nearly self-contained:
# crypt.o includes nothing at all, execlp.o only the compiler's <stdarg.h>.
execvp.o:	$(TOP)/include/errno.h
ttyname.o:	$(TOP)/include/sys/types.h $(TOP)/include/sys/param.h \
		$(TOP)/include/sys/dir.h $(TOP)/include/sys/stat.h
ttyname.o ttyslot.o: $(TOP)/include/string.h
getlogin.o:	$(TOP)/include/utmp.h
tell.o:		$(TOP)/include/sys/types.h

# stdio/.  Almost everything here includes <stdio.h>; the four pw/gr LOOKUPS do not --
# they reach the file only through the walker next door -- and naming them costs
# nothing but a rebuild they would have had anyway.
$(STDIO):	$(TOP)/include/stdio.h
filbuf.o flsbuf.o setbuf.o setvbuf.o doscan.o: $(TOP)/include/stdlib.h
endopen.o:	$(TOP)/include/errno.h
doprnt.o:	$(TOP)/include/math.h
doscan.o:	$(TOP)/include/ctype.h

system.o popen.o getpass.o: $(TOP)/include/signal.h
system.o:	$(TOP)/include/stdlib.h
getpass.o:	$(TOP)/include/sgtty.h
getpwent.o getpwnam.o getpwuid.o: $(TOP)/include/pwd.h
getgrent.o getgrnam.o getgrgid.o: $(TOP)/include/grp.h
getpwent.o getgrent.o: $(TOP)/include/stdlib.h
getpwnam.o getgrnam.o: $(TOP)/include/string.h
