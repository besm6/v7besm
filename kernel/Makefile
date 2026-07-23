# Thin wrapper over the CMake build (see CMakeLists.txt).  The real recipes -- how the
# b6* toolchain compiles, archives and links the kernel -- live there; this only drives it,
# the same way the top-level Makefile drives the cmd/ build.
#
# make        -- build the `unix' image (ends by printing `b6size -w unix')
# make run    -- boot it under SIMH (besm6 unix.ini)
# make tests  -- build the kernel/test SIMH programs, do not run them
# make test   -- build and run the test suite via ctest
# make clean  -- remove build/
# make debug  -- reconfigure as a Debug build (default is RelWithDebInfo)

.PHONY: all run tests test clean debug

all:    build
	cmake --build build

run:    all
	cmake --build build --target run

tests:  build
	cmake --build build --target tests

test:   tests
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build

debug:
	rm -rf build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug

build:
	cmake -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
