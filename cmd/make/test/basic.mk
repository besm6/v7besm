prog: a.o b.o
	ld -o prog a.o b.o
a.o: a.c hdr.h
	cc -c a.c
b.o: b.c hdr.h
	cc -c b.c
