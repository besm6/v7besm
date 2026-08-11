A = one
B = $(A) two
C = ${B} three
all:
	echo $C
	echo $@ and $$
	echo $(NOTSET)x
