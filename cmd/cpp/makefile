DESTDIR = /usr/local
CFLAGS = -O -Wall -Werror
OBJ = cpp.o parser.o yylex.o

cpp     = b6cpp

all:	cpp

clean:
	rm -f *.o cpp

cpp:    $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o cpp

install: $(DESTDIR)/bin/$(cpp)

$(DESTDIR)/bin/$(cpp): cpp
	install -s cpp $(DESTDIR)/bin/$(cpp)
