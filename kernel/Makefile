OBJ     = conf.o mch.o libsys.a libdev.a

SYS     = acct.o alloc.o clock.o fio.o iget.o machdep.o main.o \
          malloc.o nami.o pipe.o prf.o prim.o rdwri.o sig.o slp.o subr.o \
          sys1.o sys2.o sys3.o sys4.o sysent.o text.o trap.o utab.o

DEV     = bio.o cd.o fd.o hd.o md.o mem.o partab.o \
          sc.o sr.o sys.o tty.o

# Clang compiler on MacOS
LLVMBIN = $(dir $(wildcard /usr/local/Cellar/llvm@19/19.*/bin/clang-19))

AS      =  $(LLVMBIN)clang -target i486-unknown-linux-gnu -c
CC      = $(LLVMBIN)clang -target i486-unknown-linux-gnu -ffreestanding
CFLAGS  = -Os -I../include -DKERNEL -Wall -Werror -Wshadow

VPATH = dev

all:    unix
	$(LLVMBIN)llvm-size unix

clean:
	rm -f *.o *.a *.nm unix

unix:   $(OBJ)
	ld.lld -T unix.ld $(OBJ) -o $@
	$(LLVMBIN)llvm-nm -n $@ > $@.nm

libsys.a: $(SYS)
	ar cr $@ $(SYS)

libdev.a: $(DEV)
	ar cr $@ $(DEV)

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
cd.o: dev/cd.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h ../include/sys/dir.h ../include/sys/user.h
dsort.o: dev/dsort.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h
fd.o: dev/fd.c ../include/sys/param.h ../include/sys/buf.h \
  ../include/sys/dir.h ../include/sys/user.h ../include/sys/systm.h
hd.o: dev/hd.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h ../include/sys/dir.h ../include/sys/user.h \
  ../include/sys/part.h
md.o: dev/md.c ../include/sys/param.h ../include/sys/systm.h \
  ../include/sys/buf.h ../include/sys/dir.h ../include/sys/user.h
mem.o: dev/mem.c ../include/sys/param.h ../include/sys/dir.h \
  ../include/sys/user.h ../include/sys/conf.h
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
  ../include/sys/proc.h ../include/sys/inode.h \
  ../include/sys/file.h ../include/sys/reg.h ../include/sys/conf.h
