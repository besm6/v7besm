# Thin wrapper over the top-level CMake build, into which kernel/ is now integrated
# (add_subdirectory(kernel) in ../CMakeLists.txt).  It drives that one build tree
# (../build) rather than a separate kernel/build/, so `cd kernel && make' and the repo's
# top-level `make' agree on a single set of artifacts.
#
# The kernel cross-compiles with the *installed* b6* toolchain, so the top-level configure
# only wires kernel/ in once `make install' has run; before that these targets report a
# missing CMake target -- build and install the toolchain first (see the top-level README).
# kernel/ can also still be built in isolation: `cmake -S kernel -B <dir> && cmake --build <dir>'.
#
# make        -- build the `unix' image (ends by printing `b6size -w unix')
# make run    -- boot it under SIMH (besm6 unix.ini)
# make tests  -- build the kernel/test SIMH programs, do not run them
# make test   -- build and run the kernel test suite via ctest (label `kernel')
# make clean  -- remove the top-level build tree

TOP   := ..
BUILD := $(TOP)/build

.PHONY: all run tests test clean

all: | $(BUILD)
	cmake --build $(BUILD) --target kernel

run: all
	cmake --build $(BUILD) --target run

tests: | $(BUILD)
	cmake --build $(BUILD) --target tests

test: tests
	ctest --test-dir $(BUILD) --output-on-failure -L kernel

clean:
	rm -rf $(BUILD)

# Configure the top-level project if its build tree is absent (order-only, so an existing
# tree is left as configured).  Matches the top-level Makefile's own `build' rule.
$(BUILD):
	cd $(TOP) && cmake -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
