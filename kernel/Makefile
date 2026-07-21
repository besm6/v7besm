# Unix kernel
SYS     = acct.o alloc.o clock.o conf.o fio.o iget.o intr.o machdep.o main.o \
          malloc.o nami.o pipe.o prf.o prim.o rdwri.o sig.o slp.o subr.o \
          sys1.o sys2.o sys3.o sys4.o syscall.o sysent.o text.o trap.o utab.o

# Drivers
DEV     = bio.o mb.o md.o mem.o partab.o sc.o sr.o sys.o tty.o

# Machine-language assist
MACH    = brz.o uarea.o seg.o usermem.o switch.o psw.o

# besm6.o comes first: the vector block's placement at 0500/0501 depends on its
# const contribution being the first one in the image.
OBJ     = besm6.o libunix.a

AS      = b6cc -c
CC      = b6cc
CFLAGS  = -I../include -DKERNEL
AR      = b6ar
RANLIB  = b6ranlib
LD      = b6ld
NM      = b6nm
SIZE    = b6size -w
DISASM  = b6disasm
VPATH   = dev

.PHONY: all clean run

all:    unix
	$(SIZE) unix

clean:
	rm -f *.o *.a *.nm *.dis unix

# -lruntime alone, and never a -lc: the only thing the kernel takes from outside is
# the b$* compiler-support helpers every compiled function calls.  It defines its own
# printf in prf.c and references no str*/mem* at all, so lib/libc -- a user-mode
# library that traps to this very kernel -- has no business on this line.
unix:   $(OBJ)
	$(LD) $(OBJ) -o $@ -L$$HOME/.local/share/besm6/lib -lruntime
	$(NM) -n $@ > $@.nm
	$(DISASM) $@ > $@.dis

libunix.a: $(SYS) $(DEV) $(MACH)
	$(AR) cr $@ $^
	$(RANLIB) $@

# The .S sources are preprocessed (they #include sys/param.h, sys/besm6dev.h, const.s),
# so build them through the compiler driver with the include path: b6cc dispatches .S to
# cpp -> as, but .s straight to as.
.S.o:
	$(CC) -c $(CFLAGS) $< -o $@

# Run the kernel on the SIMH BESM-6 simulator.  Prints `mem = ...' from startup()
# on the operator console; interrupt with ^E, then q to stop.  See unix.ini.
run:    unix
	besm6 unix.ini

###
acct.o: acct.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/acct.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/inode.h ../include/sys/proc.h \
  ../include/sys/seg.h
alloc.o: alloc.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/mount.h ../include/sys/filsys.h \
  ../include/sys/fblk.h ../include/sys/conf.h ../include/sys/buf.h \
  ../include/sys/inode.h ../include/sys/ino.h ../include/sys/dir.h \
  ../include/sys/user.h
clock.o: clock.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/callo.h ../include/sys/seg.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/reg.h ../include/sys/besm6dev.h
conf.o: conf.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/buf.h ../include/sys/tty.h \
  ../include/sys/conf.h ../include/sys/proc.h ../include/sys/text.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/file.h \
  ../include/sys/inode.h ../include/sys/acct.h
fio.o: fio.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/filsys.h ../include/sys/file.h ../include/sys/conf.h \
  ../include/sys/inode.h ../include/sys/reg.h ../include/sys/besm6dev.h \
  ../include/sys/acct.h
iget.o: iget.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/mount.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/inode.h ../include/sys/ino.h \
  ../include/sys/filsys.h ../include/sys/conf.h ../include/sys/buf.h
intr.o: intr.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/besm6dev.h
machdep.o: machdep.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/acct.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/inode.h ../include/sys/proc.h \
  ../include/sys/seg.h ../include/sys/map.h ../include/sys/reg.h \
  ../include/sys/besm6dev.h ../include/sys/buf.h
main.o: main.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/filsys.h ../include/sys/mount.h ../include/sys/map.h \
  ../include/sys/proc.h ../include/sys/inode.h ../include/sys/seg.h \
  ../include/sys/conf.h ../include/sys/buf.h
malloc.o: malloc.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/map.h
nami.o: nami.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/inode.h ../include/sys/mount.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/buf.h
pipe.o: pipe.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/inode.h ../include/sys/file.h ../include/sys/reg.h \
  ../include/sys/besm6dev.h
prf.o: prf.c ../include/sys/types.h ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/seg.h ../include/sys/buf.h ../include/sys/conf.h
prim.o: prim.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/tty.h ../include/sys/systm.h ../include/sys/conf.h \
  ../include/sys/buf.h
rdwri.o: rdwri.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/inode.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/buf.h ../include/sys/conf.h
sig.o: sig.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/inode.h ../include/sys/reg.h \
  ../include/sys/besm6dev.h ../include/sys/text.h ../include/sys/seg.h
slp.o: slp.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/text.h ../include/sys/map.h \
  ../include/sys/file.h ../include/sys/inode.h ../include/sys/buf.h
subr.o: subr.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/conf.h ../include/sys/inode.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/buf.h
sys1.o: sys1.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/map.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/proc.h ../include/sys/buf.h \
  ../include/sys/reg.h ../include/sys/besm6dev.h ../include/sys/inode.h \
  ../include/sys/seg.h ../include/sys/acct.h
sys2.o: sys2.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/reg.h ../include/sys/besm6dev.h ../include/sys/file.h \
  ../include/sys/inode.h
sys3.o: sys3.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/mount.h ../include/sys/ino.h \
  ../include/sys/reg.h ../include/sys/besm6dev.h ../include/sys/buf.h \
  ../include/sys/filsys.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/inode.h ../include/sys/file.h ../include/sys/conf.h \
  ../include/sys/stat.h
sys4.o: sys4.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/reg.h ../include/sys/besm6dev.h ../include/sys/inode.h \
  ../include/sys/proc.h ../include/sys/timeb.h
syscall.o: syscall.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/reg.h ../include/sys/besm6dev.h \
  ../include/sys/seg.h
sysent.o: sysent.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h
text.o: text.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/map.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/proc.h ../include/sys/text.h \
  ../include/sys/inode.h ../include/sys/buf.h ../include/sys/seg.h
trap.o: trap.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/reg.h ../include/sys/besm6dev.h \
  ../include/sys/seg.h
utab.o: utab.c ../include/sys/types.h ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/text.h ../include/sys/seg.h
besm6.o: besm6.S ../include/sys/param.h ../include/sys/besm6dev.h const.s
seg.o: seg.S ../include/sys/param.h
uarea.o: uarea.S ../include/sys/param.h
usermem.o: usermem.S ../include/sys/param.h
bio.o: dev/bio.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/buf.h ../include/sys/conf.h ../include/sys/proc.h \
  ../include/sys/seg.h
mb.o: dev/mb.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/buf.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/besm6dev.h \
  ../include/sys/besm6disk.h
md.o: dev/md.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/buf.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/besm6dev.h \
  ../include/sys/besm6disk.h
mem.o: dev/mem.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/conf.h
partab.o: dev/partab.c
sc.o: dev/sc.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/conf.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/tty.h ../include/sys/systm.h ../include/sys/besm6dev.h
sr.o: dev/sr.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/conf.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/tty.h ../include/sys/systm.h
sys.o: dev/sys.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/conf.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/tty.h ../include/sys/proc.h
tty.o: dev/tty.c ../include/sys/types.h ../include/sys/param.h \
  ../include/sys/systm.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/tty.h ../include/sys/proc.h ../include/sys/inode.h \
  ../include/sys/file.h ../include/sys/reg.h ../include/sys/besm6dev.h \
  ../include/sys/conf.h
