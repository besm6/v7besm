# The user-level libraries.  See README.md for the work plan.
#
# Each library is a directory with its own Makefile, built by recursion; test/ is
# the b6sim harness that links against them.  Phase 7 appends libm, libtermlib and
# libcurses to LIBS.
#
# `test' is both a target here and a directory next to it; .PHONY is what keeps
# make from judging the directory up to date and skipping the run.

LIBS    = libc
DIRS    = $(LIBS) test

.PHONY: all test install clean

all:
	@for d in $(DIRS); do $(MAKE) -C $$d all || exit 1; done

# Run the harness.  Everything it links against has to exist first, hence `all'.
test:   all
	$(MAKE) -C test test

# Install the libraries only -- test/ is a harness, not a product.  Step 3 of the
# bootstrap; see libc/Makefile and the top-level README.md.
install:
	@for d in $(LIBS); do $(MAKE) -C $$d install || exit 1; done

clean:
	@for d in $(DIRS); do $(MAKE) -C $$d clean || exit 1; done
