# Thin wrapper over the top-level CMake build, into which kernel/ is now integrated
# (add_subdirectory(kernel) in ../CMakeLists.txt).  It drives that one build tree
# (../build) rather than a separate kernel/build/, so `cd kernel && make' and the repo's
# top-level `make' agree on a single set of artifacts.
#
# The kernel cross-compiles with the IN-TREE b6* tool targets, so no `make install' of this
# repo is needed first.  What the top-level configure does need is the EXTERNAL c-compiler:
# it wires kernel/ in only when that compiler's libruntime.a is installed, and before then
# these targets report a missing CMake target.
# kernel/ can also still be built in isolation: `cmake -S kernel -B <dir> && cmake --build <dir>'.
#
# make        -- build the `unix' image (ends by printing `b6size -w unix')
# make demo   -- boot it under SIMH (besm6 unix.ini), an interactive session
# make tests  -- build the kernel/test SIMH programs, do not run them
# make test   -- build and run the kernel tests (label `kernel')
# make clean  -- remove the top-level build tree

TOP   := ..
BUILD := $(TOP)/build

.PHONY: all demo tests test clean

all: | $(BUILD)
	cmake --build $(BUILD) --target kernel

demo: all
	cmake --build $(BUILD) --target demo

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
