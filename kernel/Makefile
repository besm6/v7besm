SYS     = acct.o alloc.o clock.o fio.o iget.o intr.o machdep.o main.o \
          malloc.o nami.o pipe.o prf.o prim.o rdwri.o sig.o slp.o subr.o \
          sys1.o sys2.o sys3.o sys4.o sysent.o text.o trap.o utab.o

DEV     = bio.o hd.o mem.o partab.o \
          sc.o sr.o sys.o tty.o

# i486: Clang compiler on MacOS
#LLVMBIN = $(dir $(wildcard /usr/local/Cellar/llvm/22.*/bin/clang-22))
#AS      = $(LLVMBIN)clang -target i486-unknown-linux-gnu -c
#CC      = $(LLVMBIN)clang -target i486-unknown-linux-gnu -ffreestanding
#CFLAGS  = -Os -I../include -DKERNEL -Wall -Werror -Wshadow
#AR      = ar
#RANLIB  = :
#LD      = ld.lld -T unix.ld
#NM      = $(LLVMBIN)llvm-nm
#SIZE    = $(LLVMBIN)llvm-size
#DISASM  = $(LLVMBIN)llvm-objdump -D
#OBJ     = conf.o x86.o libsys.a libdev.a

# Besm6
AS      = b6cc -c
CC      = b6cc
CFLAGS  = -I../include -DKERNEL
AR      = b6ar
RANLIB  = b6ranlib
LD      = b6ld
NM      = b6nm
SIZE    = b6size -w
DISASM  = b6disasm
# besm6.o comes first: the vector block's placement at 0500/0501 depends on its
# const contribution being the first one in the image.
# libsys.a is named twice: the archives are scanned once each, in order, and the
# drivers in libdev.a call back into the core kernel (timeout(), wakeup(), ...), so
# a member of libsys.a can be needed only after libdev.a has been read.
OBJ     = besm6.o brz.o conf.o libsys.a libdev.a libsys.a

VPATH = dev

all:    unix
	$(SIZE) unix

clean:
	rm -f *.o *.a *.nm unix

unix:   besm6.o brz.o conf.o libsys.a libdev.a
	$(LD) $(OBJ) -o $@ -L$$HOME/.local/share/besm6/lib -lc
	$(NM) -n $@ > $@.nm
	$(DISASM) $@ > $@.dis

libsys.a: $(SYS)
	$(AR) cr $@ $(SYS)
	$(RANLIB) $@

libdev.a: $(DEV)
	$(AR) cr $@ $(DEV)
	$(RANLIB) $@

%.ast:  %.c
	$(CC) -Xclang -ast-dump=json -fsyntax-only $(CFLAGS) $< > $@

%.i:    %.c
	cpp -E $(CFLAGS) $< > $@

%.yaml: %.i
	cast --yaml $<

yaml:   $(patsubst %.o,%.yaml,$(SYS) $(DEV))

i:      $(patsubst %.o,%.i,$(SYS) $(DEV))

###
acct.o: acct.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/acct.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/inode.h ../include/sys/proc.h ../include/sys/seg.h
alloc.o: alloc.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/mount.h ../include/sys/filsys.h ../include/sys/fblk.h \
  ../include/sys/conf.h ../include/sys/buf.h ../include/sys/inode.h \
  ../include/sys/ino.h ../include/sys/dir.h ../include/sys/user.h
clock.o: clock.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/callo.h ../include/sys/seg.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/proc.h ../include/sys/reg.h
conf.o: conf.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h ../include/sys/tty.h ../include/sys/conf.h \
  ../include/sys/proc.h ../include/sys/text.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/file.h ../include/sys/inode.h \
  ../include/sys/acct.h
fio.o: fio.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/filsys.h \
  ../include/sys/file.h ../include/sys/conf.h ../include/sys/inode.h \
  ../include/sys/reg.h ../include/sys/acct.h
iget.o: iget.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/mount.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/inode.h ../include/sys/ino.h ../include/sys/filsys.h \
  ../include/sys/conf.h ../include/sys/buf.h
machdep.o: machdep.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/acct.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/inode.h ../include/sys/proc.h ../include/sys/seg.h \
  ../include/sys/map.h ../include/sys/reg.h ../include/sys/buf.h
main.o: main.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/filsys.h \
  ../include/sys/mount.h ../include/sys/map.h ../include/sys/proc.h \
  ../include/sys/inode.h ../include/sys/seg.h ../include/sys/conf.h \
  ../include/sys/buf.h
malloc.o: malloc.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/map.h
nami.o: nami.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/inode.h ../include/sys/mount.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/buf.h
pipe.o: pipe.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/inode.h \
  ../include/sys/file.h ../include/sys/reg.h
prf.o: prf.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/seg.h ../include/sys/buf.h ../include/sys/conf.h
prim.o: prim.c ../include/sys/param.h ../include/sys/tty.h \
  ../include/sys/systm.h ../include/sys/conf.h ../include/sys/buf.h
rdwri.o: rdwri.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/inode.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/buf.h ../include/sys/conf.h
sig.o: sig.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/inode.h ../include/sys/reg.h ../include/sys/text.h \
  ../include/sys/seg.h
slp.o: slp.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/text.h ../include/sys/map.h ../include/sys/file.h \
  ../include/sys/inode.h ../include/sys/buf.h
subr.o: subr.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/conf.h ../include/sys/inode.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/buf.h
sys1.o: sys1.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/map.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/buf.h ../include/sys/reg.h \
  ../include/sys/inode.h ../include/sys/seg.h ../include/sys/acct.h
sys2.o: sys2.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/reg.h \
  ../include/sys/file.h ../include/sys/inode.h
sys3.o: sys3.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/mount.h ../include/sys/ino.h ../include/sys/reg.h \
  ../include/sys/buf.h ../include/sys/filsys.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/inode.h ../include/sys/file.h \
  ../include/sys/conf.h ../include/sys/stat.h
sys4.o: sys4.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/reg.h \
  ../include/sys/inode.h ../include/sys/proc.h ../include/sys/timeb.h
sysent.o: sysent.c ../include/sys/param.h ../include/sys/systm.h
text.o: text.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/map.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/proc.h ../include/sys/text.h ../include/sys/inode.h \
  ../include/sys/buf.h ../include/sys/seg.h
trap.o: trap.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/reg.h ../include/sys/seg.h
utab.o: utab.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/proc.h \
  ../include/sys/text.h ../include/sys/seg.h
bio.o: dev/bio.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/buf.h \
  ../include/sys/conf.h ../include/sys/proc.h ../include/sys/seg.h
hd.o: dev/hd.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/part.h
mem.o: dev/mem.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/conf.h
partab.o: dev/partab.c
sc.o: dev/sc.c ../include/sys/param.h ../include/sys/conf.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/tty.h \
  ../include/sys/systm.h
sr.o: dev/sr.c ../include/sys/param.h ../include/sys/conf.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/tty.h \
  ../include/sys/systm.h
sys.o: dev/sys.c ../include/sys/param.h ../include/sys/conf.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/tty.h \
  ../include/sys/proc.h
tty.o: dev/tty.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/tty.h \
  ../include/sys/proc.h ../include/sys/inode.h ../include/sys/file.h \
  ../include/sys/reg.h ../include/sys/conf.h
