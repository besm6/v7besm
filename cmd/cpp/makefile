CFLAGS = -O -Werror -fdiagnostics-color
OBJ = cpp.o parser.o yylex.o

all:	cpp

clean:
	rm -f *.o cpp

cpp:    $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o cpp
