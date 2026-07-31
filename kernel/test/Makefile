# Thin wrapper delegating to the kernel's CMake build one level up, which configures the
# tests through test/CMakeLists.txt.  Kept so `cd kernel/test && make test' still works.
#
# make        -- build the SIMH test programs
# make test   -- build them and run the fast ones via ctest
# make weekly -- build them and run the slow ones: the tests that boot the kernel
# make clean  -- remove the build tree

.PHONY: all test weekly clean

all:
	$(MAKE) -C .. tests

test:
	$(MAKE) -C .. test

weekly:
	$(MAKE) -C .. weekly

clean:
	$(MAKE) -C .. clean
