OBJ     = conf.o mch.o libsys.a libdev.a

SYS     = acct.o alloc.o clock.o fakemx.o fio.o iget.o machdep.o main.o \
          malloc.o nami.o pipe.o prf.o prim.o rdwri.o sig.o slp.o subr.o \
          sys1.o sys2.o sys3.o sys4.o sysent.o text.o trap.o utab.o

DEV     = bio.o cd.o dsort.o fd.o hd.o md.o mem.o mx1.o mx2.o partab.o \
          pk0.o pk1.o pk2.o pk3.o sc.o sr.o sys.o tty.o

# Clang compiler on MacOS
LLVMBIN = $(dir $(wildcard /usr/local/Cellar/llvm@19/19.*/bin/clang-19))

CC      = $(LLVMBIN)clang -std=c89 -target i486-unknown-linux-gnu -ffreestanding \
            -I../include -Wno-deprecated-non-prototype -Wno-implicit-function-declaration \
            -Wno-return-mismatch -Wno-return-type -Wno-parentheses -Werror
AS      =  $(LLVMBIN)clang -target i486-unknown-linux-gnu -c

VPATH = dev

all:    unix
	$(LLVMBIN)llvm-size unix

clean:
	rm -f *.o *.a *.nm unix mkconf

unix:   $(OBJ)
	ld.lld -T unix.ld $(OBJ) -o $@
	$(LLVMBIN)llvm-nm -n $@ > $@.nm

libsys.a: $(SYS)
	ar cr $@ $(SYS)

libdev.a: $(DEV)
	ar cr $@ $(DEV)

mkconf: conf/mkconf.c
	cc -O -o $@ $<
