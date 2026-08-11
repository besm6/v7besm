.SUFFIXES: .o .c
.c.o:
	cc -c $< -o $@
x.c:
	echo no-op
all: x.o
	echo linked $?
