# The BESM-6 Unix port: what is left

**Nothing.** The kernel work plan is empty: every task through 39 is done, and each writeup was
removed as it was finished. The numbering is **left as it was** — task numbers are cited from the
sources and from [../doc/](../doc/), so a new task takes the next free number rather than a
retired one.

**[README.md](README.md) is the reference** — where the port stands, the design it settled on, the
five hardware rules, the u-area invariant, what a standalone SIMH test costs to get right, the
gotchas, and the **consequences deliberately accepted**, which is where the things this kernel
will not do are written down. How each task turned out is in the source comments and in
[../doc/](../doc/). The userland plan, which is not empty, is [../cmd/TODO.md](../cmd/TODO.md).

A new task belongs here, and it leaves the tree building (`cd kernel && make`) and the suite
passing (the top-level `make run`; a bare `ctest -L kernel` builds nothing and tests a stale
image). Verification is under SIMH via `test/*.ini` — `b6sim` runs a user `a.out` with no kernel
underneath and cannot exercise any of it. `test/mmutest` is the model to copy, every MMU test runs
with `set mmu cache`, and README.md's "Writing a standalone SIMH test" is the rest of that story,
including why `besm6.o` cannot go into one.
