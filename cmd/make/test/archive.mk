bymember: libmk.a(mkzork.o)
	echo member resolved
byentry: libmk.a((mkzork))
	echo entry resolved
missing: libmk.a(nosuch.o)
	echo never
