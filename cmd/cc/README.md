# b6cc — C compiler driver for the BESM-6

`b6cc` turns C source into BESM-6 `a.out` objects and executables. It is a **driver**, not a
compiler itself — a modern C11 rewrite of the Unix v7 `cc(1)` driver that chains the toolchain
one sub-tool per stage.

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
sub-tool is resolved from `~/.local/bin` then `/usr/local/bin`, and can be overridden with a
per-tool environment variable (`B6CPP`, `B6PARSE`, `B6LOWER`, `B6CODEGEN`, `B6AS`, `B6LD`).

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
(`<prefix>/share/besm6/include`). `-nostdinc` suppresses that; any user `-I` directories are still
passed through.

## Linking

When linking (no `-E`/`-S`/`-c`), `b6cc` invokes `b6ld` with `-e _start` and:

- prepends the **startup object** `crt0.o`, located under `<prefix>/share/besm6/lib` —
  `~/.local/share/besm6/lib` is tried first, then `/usr/local/share/besm6/lib`. A missing
  `crt0.o` is a fatal error;
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
