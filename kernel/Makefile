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
# make run    -- boot it under SIMH (besm6 unix.ini)
# make tests  -- build the kernel/test SIMH programs, do not run them
# make test   -- build and run the fast kernel tests (label `kernel' less `weekly')
# make weekly -- build and run the slow ones: the tests that boot the kernel under SIMH and
#                drive it by typing at it.  About a minute, serialized by a resource lock,
#                and deliberately out of the daily loop -- run it before a commit that
#                touches the kernel, the drivers or the root image.
# make clean  -- remove the top-level build tree

TOP   := ..
BUILD := $(TOP)/build

.PHONY: all run tests test weekly clean

all: | $(BUILD)
	cmake --build $(BUILD) --target kernel

run: all
	cmake --build $(BUILD) --target run

tests: | $(BUILD)
	cmake --build $(BUILD) --target tests

test: tests
	ctest --test-dir $(BUILD) --output-on-failure -L kernel -LE weekly

# Hangs on `tests' for the reason `test' does -- ctest never builds -- and it matters more
# here: this is exactly the set that boots root.img, so a stale image would make every one
# of these test yesterday's tree.
weekly: tests
	ctest --test-dir $(BUILD) --output-on-failure -L weekly

clean:
	rm -rf $(BUILD)

# Configure the top-level project if its build tree is absent (order-only, so an existing
# tree is left as configured).  Matches the top-level Makefile's own `build' rule.
$(BUILD):
	cd $(TOP) && cmake -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
