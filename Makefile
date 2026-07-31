#
# make
# make all   -- build everything
#
# make test  -- build all unit tests, do not run
#
# make run   -- run the unit tests, minus the slow SIMH boot tests
#
# make weekly -- run only those: the tests that boot the kernel under SIMH and type at it.
#                About a minute, serialized by a resource lock, and not part of the daily
#                loop.  See kernel/test/CMakeLists.txt for what carries the label.
#
# make clean -- remove build files
#
# To reconfigure for Debug build:
#   make clean; make debug; make
#
all:    build
	$(MAKE) -Cbuild $@

test:   build
	$(MAKE) -Cbuild build_tests

run:    test
	ctest --test-dir build --progress -LE weekly

weekly: test
	ctest --test-dir build --progress --output-on-failure -L weekly

install: all
	@prefix=$$( [ -d "$$HOME/.local" ] && echo "$$HOME/.local" || echo /usr/local ); \
	echo "Installing to $$prefix"; \
	cmake --install build --prefix "$$prefix"

clean:
	rm -rf build

build:
	mkdir $@
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	mkdir build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
