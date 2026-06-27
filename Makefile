#
# make
# make all   -- build everything
#
# make test  -- build all unit tests, do not run
#
# make run   -- run all unit tests
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
	ctest --test-dir build

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
