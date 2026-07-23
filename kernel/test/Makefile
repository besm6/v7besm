# Thin wrapper delegating to the kernel's CMake build one level up, which configures the
# tests through test/CMakeLists.txt.  Kept so `cd kernel/test && make test' still works.
#
# make       -- build the SIMH test programs
# make test  -- build them and run the suite via ctest
# make clean -- remove the build tree

.PHONY: all test clean

all:
	$(MAKE) -C .. tests

test:
	$(MAKE) -C .. test

clean:
	$(MAKE) -C .. clean
