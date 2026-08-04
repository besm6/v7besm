# cc — C compiler driver for the BESM-6

`b6cc` turns C source into BESM-6 `a.out` objects and executables. It is a **driver**, not a
compiler itself — a modern C11 rewrite of the Unix v7 `cc(1)` driver that chains the toolchain
one sub-tool per stage.

**One source, two programs.** The same `cc.c` is built as the host tool `b6cc` and as the
machine's own `/usr/bin/cc` (task C9e, [../TODO.md](../TODO.md)). Where this page says
`b6cpp`, `~/.local/bin` or `share/besm6/lib`, the native build reads `cpp`, `/usr/bin` and
`/lib`; "Building for the BESM-6" below is the whole of the difference, and the one thing
the machine cannot do.

## Pipeline

```text
b6cpp      preprocess    .c   -> .i
b6parse    parse         .i   -> .ast
b6lower    lower + opt   .ast -> .tac
b6codegen  code gen      .tac -> .s
b6as       assemble      .s   -> .o
b6ld       link          .o   -> a.out
```

`b6parse`, `b6lower`, and `b6codegen` are the three passes of the external
[c-compiler](https://github.com/besm6/c-compiler/); the rest are tools from this repo. Each
sub-tool is resolved from `~/.local/bin` then `/usr/local/bin` — from `/usr/bin` alone on the
machine itself, where the names carry no `b6` prefix — and can be overridden with a per-tool
environment variable (`B6CPP`, `B6PARSE`, `B6LOWER`, `B6CODEGEN`, `B6AS`, `B6LD`). **The
overrides work in both builds and are not keyed on anything**: they are how a test points
either driver at a tool that is not on its search path.

## Input files

Inputs are dispatched by suffix:

| Suffix | Handling                                        |
|--------|-------------------------------------------------|
| `.c`   | full pipeline (preprocess → … → assemble/link)  |
| `.S`   | preprocessed assembly (`b6cpp` → `b6as`)        |
| `.s`   | assembly (`b6as` only)                          |
| `.o`   | object, passed straight to the linker           |

## Options

| Option        | Meaning                                                   |
|---------------|-----------------------------------------------------------|
| `-c`          | Compile and assemble, but do not link                     |
| `-S`          | Compile only; emit assembly (`.s`)                        |
| `-Sbemsh`     | Like `-S`, but emit Bemsh-dialect assembly                |
| `-Smadlen`    | Like `-S`, but emit Madlen-dialect assembly               |
| `-E`          | Preprocess only; write to output or `.i`                  |
| `-o file`     | Set the output file name                                  |
| `-O`          | Optimize (reserved; currently a no-op)                    |
| `-g`          | Emit debug info (reserved; currently a no-op)             |
| `-v`          | Verbose: echo each sub-command before running it          |
| `-Dname[=v]`  | Predefine a preprocessor macro                            |
| `-Uname`      | Undefine a preprocessor macro                             |
| `-Ipath`      | Add a header search directory                             |
| `-Lpath`      | Add a library search directory (passed to the linker)     |
| `-lname`      | Link against library `libname` (passed to the linker)     |
| `-nostdlib`   | No `crt0.o`, no library dirs, no `-lc`/`-lruntime`        |
| `-nostdinc`   | Do not add the standard system include directory          |

The last stage to run is selected by `-E` (stop after preprocessing), `-S` (stop after code
generation, emit assembly), and `-c` (stop after assembling, emit an object). With none of these,
the objects are linked into an executable.

`-Sbemsh` and `-Smadlen` stop after code generation just like `-S`, but additionally pass
`--bemsh`/`--madlen` to `b6codegen` to select the emitted assembler dialect. Plain `-S` passes no
dialect flag, so `b6codegen` uses its own default. When no `-o` name is given, the derived output
file uses a dialect-matching extension: `.bemsh` for `-Sbemsh`, `.madlen` for `-Smadlen`, and `.s`
for plain `-S`.

When preprocessing, `b6cc` automatically adds the standard BESM-6 system include directory
(`<prefix>/share/besm6/include`; `/usr/include` on the machine). `-nostdinc` suppresses that;
any user `-I` directories are still passed through.

## Linking

When linking (no `-E`/`-S`/`-c`), `b6cc` invokes `b6ld` with `-e _start` and:

- prepends the **startup object** `crt0.o`, located under `<prefix>/share/besm6/lib` —
  `~/.local/share/besm6/lib` is tried first, then `/usr/local/share/besm6/lib`, and `/lib`
  alone on the machine. A missing `crt0.o` is a fatal error;
- adds the standard library search directories (`-L…`) for those same prefixes;
- closes the line with **two** implicit archives, `-lc` then `-lruntime`.

`libc.a` and `crt0.o` are this repository's own, built by [`lib/libc`](../../lib/) and installed
into `share/besm6/lib` by `make install`. `libruntime.a` beside them is the external
[c-compiler](https://github.com/besm6/c-compiler/)'s, and holds the `b$*` compiler-support
helpers (`b$save`, `b$ret`, `b$mul`, …) that every compiled function calls — it is the one piece
of the link that cannot come from here.

The order of the two is a contract, not a preference: `b6ld` scans an archive once, where it
stands on the line, so `libc.a` must come first — libc calls the helpers, and no helper calls
back into libc.

A "crt0.o not found" error almost always means the library has never been installed, not that a
freestanding link was wanted; `make && make install` is the fix (it builds and installs `lib/`
along with the tools), as the top-level [README](../../README.md) describes.

`-nostdlib` suppresses all of the above for a freestanding link (no `crt0.o`, no lib dirs,
neither `-l`), so a missing `crt0.o` is not an error in that mode.

## Reserved options

`-O` and `-g` are accepted for compatibility but are currently no-ops: no optimizer mapping onto
`b6lower` and no debug-info format have been defined for the BESM-6 toolchain yet.

## Build & test

`b6cc` is built by the top-level `make` (installed as `b6cc`; see the repo
[README](../../README.md) and [CLAUDE.md](../../CLAUDE.md) for the build system). Its end-to-end
test suite is [test/cc_test.cpp](test/cc_test.cpp), run by `make run`.

## Building for the BESM-6

`cc` is also built **natively**, into `build/rootfs/usr/bin/cc` and onto the disk image —
task **C9e** of [../TODO.md](../TODO.md), the last of C9 and the driver that runs everything
the other ten tasks put there. [rootfs/CMakeLists.txt](rootfs/CMakeLists.txt) is the whole of
the build machinery; there is no second copy of the source and no `#ifdef` outside the one
profile at the head of [cc.c](cc.c).

It is the simplest of the ten native tools in two ways nothing before it was. It is **one
source file with no private header**, so the coarse header dependency is the system tree and
nothing else; and it is the **first that does not link `cmd/libaout`** — a driver reads no
`a.out`, it only hands file names to programs that do.

### The profile

|                        | host                                            | BESM-6         | why |
| ---------------------- | ----------------------------------------------- | -------------- | --- |
| sub-tool directories   | `$HOME/.local/bin`, `/usr/local/bin`            | `/usr/bin`     | there is no `$HOME` prefix on the image and no second toolchain to search past |
| sub-tool names         | `b6cpp`, `b6as`, `b6ld`, …                      | `cpp`, `as`, `ld`, … | the `b6` prefix exists to tell these apart from the *host's* own `cpp`/`as`/`ld`; on the machine there are no others |
| system include dir     | `<prefix>/share/besm6/include`                  | `/usr/include` | where v7 kept it, and where the image now carries it |
| library dir and `crt0.o` | `<prefix>/share/besm6/lib`                    | `/lib`         | likewise |

That is the entire table. **No size profile** — `cc` keeps no big table of anything, and the
ceilings that dominated `cpp`, `as` and `ld` never came near it. The environment overrides are
deliberately *not* in the table: they are checked ahead of the search in both builds.

### Two things that are the same in both builds, and one of them by choice

**`fork`, `execv` and `wait`.** `run()` used `posix_spawn()` and `waitpid()`, and this kernel
has neither — [`<sys/wait.h>`](../../include/sys/wait.h) records that the only gate is the
argument-less `wait(2)`, with no `wait3()` and no `WNOHANG`. The rewrite is what v7's own
`cc.c` did, and it went into **both** builds rather than behind an `#if besm6`: the driver has
one child in flight at a time, so the `while ((w = wait(&status)) != pid)` loop is exact on the
host too, and a second implementation would be a second thing to be wrong. The one line the
machine really contributed is the `fflush(stdout)` before the fork — `stdout` is buffered, and
without it the child inherits a copy of whatever `-v` had put in the buffer.

**Diagnostics.** Every message prints `basename(argv[0])`, which is `cc` in both worlds, and
every path the driver invents is derived from an argument. That is what lets the agreement
suite compare standard output byte for byte without masking anything.

### Three gaps, and all three were closed in libc

The C9d rule, and the property the whole of C9 is arranged around: a routine a native port
wants belongs in the library, not behind an `#if besm6` in the program. Closing these three
left `cc.c`'s spawn code, its temp-file management and the whole of `main()`
character-identical in both builds, which no `#ifdef` here could have done.

* **`strdup()`** — nine call sites, and this libc had never had it. [cmd/quot](../quot/quot.c)
  had written its own three lines of it by hand, as v7 did wherever it was wanted.
  [lib/libc/gen/strdup.c](../../lib/libc/gen/strdup.c), declared in
  [`<string.h>`](../../include/string.h) where POSIX puts it, `string(3)`, cases in
  `lib/test/strings`.
* **`mkstemps()`** — `make_temp()` names its temporaries `/tmp/ccXXXXXX.i`, `.ast`, `.tac`,
  `.s`, because **every stage of this pipeline is a file whose suffix says what is in it**, and
  `mkstemp()` cannot fill a run of `X` that is not at the end of the template. It generalises
  [lib/libc/gen/mkstemp.c](../../lib/libc/gen/mkstemp.c) in place — the walk became
  `mkstemps(as, suffixlen)` and `mkstemp(as)` became `mkstemps(as, 0)` — so the two share one
  object, one man page and the same honest caveat: there is no `O_EXCL` in this kernel and the
  creation is not atomic.
* **`atexit()`** — declared in [`<stdlib.h>`](../../include/stdlib.h) since these headers were
  written and **never implemented**, so `cc.c:atexit(cleanup)` was a link failure rather than a
  compile error. [lib/libc/gen/atexit.c](../../lib/libc/gen/atexit.c) holds 32 slots, C11
  §7.22.4.2's own minimum, walked in reverse. It arms a **pointer** that `exit()` tests, which
  is the bargain [cuexit.c](../../lib/libc/gen/cuexit.c) already struck for the stdio flush and
  for the same reason: `exit()` is tail-jumped to by `crt0` and is in every program, so naming
  the table outright would put 32 words of bss into `hello`. The order is C11's — handlers
  first, streams flushed after, because a handler is allowed to print.

### It cannot compile C

**`cc foo.c` does not work on the machine, and that is not a defect of this port.** `b6parse`,
`b6lower` and `b6codegen` are three programs of the external
[c-compiler](https://github.com/besm6/c-compiler/) repository, which defines the language this
tree is written in; nothing here can add a `b6_prog()` to them, and no task proposes bringing
them over. So the native driver says so, before the preprocessor runs rather than after:

```
$ cc hello.c
cc: error: hello.c: cannot compile C on this machine -- the parse, lower and codegen
passes are not here.  -E works, and so do .s, .S and .o inputs.
```

The diagnostic comes **first** on purpose. Letting the pipeline preprocess a file it can never
compile would leave a temporary behind and then report a missing `/usr/bin/parse`, which reads
like something an installation could fix. `cmd_cc_cfile` pins the sentence.

What *does* work is the rest of the chain, which is the reason to have the driver at all:

```
$ cc -E foo.c              # /usr/bin/cpp, with /usr/include on the search path
$ cc -c foo.S              # cpp, then as
$ cc -c foo.s              # as
$ cc -o hello hello.s      # as, then ld: /lib/crt0.o, the objects, -lc -lruntime
$ ./hello
```

That last line is the closing claim of C9: the machine assembles and links a program of its
own with its own tools, against its own C library, and runs the result.

### The demo on the image

The image ships one source file, [hello.S](hello.S), as **`/usr/guest/hello.S`** — the thing
to point `cc` at:

```
$ cc hello.S
$ ./a.out
Hello BESM-6!
```

It calls `write(2)` and `exit(2)` through the `$77` gate directly and takes nothing from
libc, so it is legible as a first program: arguments 1…n−1 pushed below `r15` with `xts`,
the last left in the accumulator, and the system call number carried in the instruction's
own address field. [kernel/test/coninit.S](../../kernel/test/coninit.S) is the longer worked
example, and `hello.S`'s own header carries the rest — including the two things a reader
coming from another machine gets wrong: **nothing sets `r14`** (the gate takes the arity
from `sysent[]` and leaves *errno* there), and **the `char *` is fat**, built at assembly
time out of a marker bit and a byte offset.

Three deliberate choices in it:

* **A capital `.S`.** It is what sends the file through `cpp` before `as`, and the
  `#include <sys/syscall.h>` at its head is what that is for — the header is `#define`-only
  precisely so an assembler may include it. So this one command is the longest chain the
  machine can run, and the only one it can run end to end.
* **It defines `main`, not `_start`.** `_start` belongs to `/lib/crt0.o`, which `cc` links
  ahead of everything unless `-nostdlib`; defining it here too is a duplicate symbol. Since
  `main` leaves through `exit(2)` rather than returning, it needs no prologue and no
  epilogue. `cc -nostdlib hello.S` with `_start` in place of `main` works as well and is the
  whole of the program — **9 words against 47** — though the gap is that small only because
  this one calls nothing: `crt0` and the `exit()` behind it are all libc contributes.
* **It lives in the guest account's home**, which is the only directory on the image a
  non-root user can write in besides `/tmp` — and `cc` writes `hello.o` and `a.out` into the
  *current* directory, so that is where a logged-in user can build it where it sits.

Section 6 of `kernel/test/toolchain` builds it exactly as its header says to, and is the
only place `cpp` and `as` run in one command anywhere.

### /lib and /usr/include

The link line has to have something to name, so C9e put the other two thirds of a development
system on the image beside the toolchain: `/lib/{crt0.o,libc.a,libruntime.a}` and the whole
system header tree at `/usr/include`. Both are staged by the top-level
[CMakeLists.txt](../../CMakeLists.txt) (the `B6_STAGE_*` lists) and listed in
[root.manifest](../../root.manifest); `kernel/test/CMakeLists.txt` hangs `root.img` on the same
lists, so a file staged and not listed cannot slip through.

`/lib` is **exactly what the default link line names** and nothing more. `libm.a`,
`libcurses.a` and `libtermcap.a` are absent — some 40 blocks for archives nothing on this image
links today, and adding one is a line in `B6_STAGE_LIB` and a stanza in the manifest.

`/usr/include` has **two owners**, which is the thing to know before adding to it: the hosted
half is this repo's [include/](../../include/) and is staged from the *source* tree, so an edit
is on the image without an install; the freestanding ten — `<stddef.h>`, `<stdarg.h>`,
`<besm6.h>` and their fellows — are the external compiler's, which defines them, and are copied
from where it installed them. The image cannot carry one half without the other: no `<stddef.h>`
means no `<stdio.h>` either.

The three cost **201 of the 793 blocks the image had free**, and 592 are left.

### The measurements

`b6size -w build/rootfs/usr/bin/cc`, in 48-bit words:

| const | text | data | bss | total |
| ----: | ---: | ---: | --: | ----: |
| 92 | 4,385 | 724 | 1,097 | **6,298** |

against a ceiling of 28,672. It sits with C9c's four (5,068–6,439) rather than with `cpp` and
`ld` near 24,000, and for the reason [../README.md](../README.md) §6 gives: **what a program
prints with dominates what it does.** Nearly all of the 6,298 is `stdio` — the driver's own
code is argument parsing, four `access()` loops and a `fork`.

#### The stack

The deepest path is not a compile at all but a *diagnostic*, since `_doprnt` is the largest
frame in the program by a factor of four:

```
main            156
  compile_one    59
    run_cpp      32
      run        64
        error    12
          fprintf   2
            vfprintf 6
              _doprnt   281
                _flsbuf 112
                              = 724 words
```

of a 4,096-word stack. `cvt` (179) sits under `_doprnt` for `%e`/`%f`/`%g` and would take it to
about 790; nothing here prints a float, but the path is there. `link_objects` (57) and
`make_temp` (27) → `mkstemps` (36) are shallower than the compile path above them. Nothing in
this program recurses.

#### The heap

`rootfs_cc_size` cannot see a byte of it, and here there is very little to see. The driver
allocates six small `struct vec` backing arrays that double from 8 pointers, and one string per
generated file name — tens of words, well inside `malloc`'s first 1,024-word page. It opens no
`FILE` of its own: `stdout` takes one `BUFSIZ` (3,072 bytes, 512 words) on the first thing it
prints, which for a successful run is nothing at all. This is the opposite of
[ld](../ld/README.md), whose twelve open streams were 6,144 words of heap before it read a byte.

### Testing the native build

Two suites, asserting different things, and [rootfs/test/](rootfs/test/) holds both.

**The agreement tests, `rootfs_cc_*`.** Host `b6cc` and native `cc` over one set of inputs, the
files the two runs produced compared byte for byte — `-c` on a `.s` and on a `.S`, `-E`, `-D`
and `-I` in their separated form, a link, and two compiles followed by a link. What is under
test is **the command lines, not the bytes**: `cpp`, `as` and `ld` have agreed with their native
selves since C9a and C9b, so an identical object says the driver named the same input, the same
output and the same flags at every stage. That is the whole job of a driver.

Three things the harness does, each for a reason worth knowing:

* **Both sides are pinned with the `B6*` overrides, and on the native side that is a
  correctness requirement rather than a convenience.** `b6sim` resolves an `exec` on the *host*
  filesystem, and `/usr/bin/cc`'s search path is `/usr/bin` — so an unpinned run would hand the
  fixture to the build machine's own assembler. [cmd/sim/session.cpp](../sim/session.cpp) carries
  the six names on `ENV_WHITELIST` for exactly this; `cmd/sh`'s harness meets the same hazard
  with `PATH` and answers it by copying `./echo` in beside the script.
* **Every link is `-nostdlib`, with `crt0.o`, `-L` and the two `-l`s named by hand.** The
  implicit search is the one thing that cannot be compared: the native driver looks in `/lib`,
  which under `b6sim` is the *build machine's* `/lib`, while the host driver looks in
  `~/.local/share/besm6/lib` and finds a real `crt0.o`. Naming the same files on both command
  lines is what makes the comparison mean something, and it still exercises the crt0 argument,
  the object order, the `-L` pass-through and the `-lc -lruntime` that closes the line.
* **The exit statuses are compared as well as the bytes.** A driver reports through its status,
  and two runs that print alike and exit differently are not agreement.

**Nothing is masked**, which is worth saying beside [ranlib](../ranlib/README.md), whose suite
has to blank six bytes of every archive: this driver stamps nothing. Its output is whatever the
stages wrote, the stages write no timestamp, and the only place a temporary's name reaches the
output is under `-v`, which no case runs.

**The behavioural cases, `cmd_cc_*`,** are ordinary `b6sim` cases with a checked-in `.expected`,
and they pin what the agreement tests cannot: the diagnostics and the exit status, which two
`cc`s wrong in the same way would agree on. They run under `env -i` with no overrides at all,
so **every one of them has to fail before any `exec`** — which is also why the `.c` diagnostic
had to come before the preprocessor rather than after it.

The fixture that is *not* named `agree.S` is this project's own hazard rather than a
preference: the tree lives on a case-insensitive filesystem, where `agree.S` and `agree.s` are
one file. `cc`'s own `same_file()` guard exists for the same reason.

### …and the one test that had to be a boot

Everything above pins the sub-tools, which is the right way to compare two drivers and is
exactly what leaves **`cc`'s own search unasserted** — and with it `/lib` and `/usr/include`,
which exist for nothing else. [kernel/test/toolchain](../../kernel/test/toolchain.sh) is the
answer: a SIMH boot of its own (volume 3102, label `weekly`) in which nothing is pinned and
nothing is named. The machine runs

```
cc -o hello hello.s
./hello
```

finds `/usr/bin/{cpp,as,ld}` by itself, finds `/lib/crt0.o`, `/lib/libc.a` and
`/lib/libruntime.a` by itself, forks and execs three sub-programs through the real kernel,
and **executes an `a.out` nothing on the host has touched**. It is also the only place the
`mkstemps()` temporaries and the `atexit()` handler that removes them are visible: section 4
prints the `/tmp/cc*` glob precisely because it must match nothing.

The fixture is `lib/test/hello.c` compiled to assembly by the *host* `b6cc` and grafted in,
because the one step of the chain this machine cannot take is the first. And the log carries
**no segment size and no address**: every such number is a property of `crt0.o` and `libc.a`
rather than of the fixture, so a checked-in one would turn an edit to the C library into a
failure of the toolchain test. What the bytes are is settled live instead —
`run-toolchain.sh` extracts the program the machine built and compares it with the host
build of the same `hello.s`, byte for byte. That comparison is **the closing claim of C9**,
and it needs no expectation file at all.

Two details in it are load-bearing and neither is tidiness. The host side names the *build
tree's* `crt0.o` and `libc.a` with `-nostdlib`, because `b6cc`'s own search would find the
**installed** `share/besm6/lib` — whatever `make install` last put there, routinely a
different build from the one staged onto the image minutes earlier. And the fixture is called
`hello.s` on both sides, in a directory of its own on the host, because **`b6as` records the
source file name in the object's symbol table**: the same assembly under two names produces
two different string tables and a header field that sizes them.
