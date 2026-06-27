#
# Assembler for BESM-6
#
DESTDIR = /usr/local
CFLAGS  = -I../../cross -O0 -g -Werror -Wall
LDFLAGS =
VPATH   = ../libaout

as      = besm6-as

OBJS    = as.o fgeth.o fputh.o fputhdr.o fputsym.o

all:    as

as:     $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o as

install: $(DESTDIR)/bin/$(as)

clean:
	rm -f *.o *.b a.out core as

$(DESTDIR)/bin/$(as): as
	install -s as $(DESTDIR)/bin/$(as)

###
