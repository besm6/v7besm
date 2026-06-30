#
# Linker for BESM-6 a.out objects.
#
# The companion utilities (ar, ranlib, strip, nm, size, lorder) now live in
# their own directories under cmd/ and are built via CMake.
#
DESTDIR = /usr/local
CFLAGS  = -I.. -Werror
LDFLAGS =

ld      = b6ld

all:    ld

ld:     ld.o fgetarhdr.o fgetint.o fgetsym.o fputsym.o fgethdr.o fputhdr.o\
		fgeth.o fputh.o fgetran.o
	$(CC) $(LDFLAGS) ld.o -o ld fgetarhdr.o fgetint.o fgetsym.o fputsym.o\
		fgethdr.o fputhdr.o fgeth.o fputh.o fgetran.o

install: $(DESTDIR)/bin/$(ld)

clean:
	rm -f *.o *.b a.out core ot ld

$(DESTDIR)/bin/$(ld): ld
	install -s ld $(DESTDIR)/bin/$(ld)

###
