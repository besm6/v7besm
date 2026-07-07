# b6cc — C compiler driver for the BESM-6

`b6cc` turns C source into BESM-6 `a.out` objects and executables. It is a **driver**, not a
compiler itself — a modern C11 rewrite of the Unix v7 `cc(1)` driver that chains the toolchain
one sub-tool per stage.

## Pipeline

```text
b6cpp   preprocess    .c   -> .i
b6parse parse         .i   -> .ast
b6lower lower + opt   .ast  -> .tac
b6codegen code gen    .tac -> .s     (Madlen assembly)
b6as    assemble      .s   -> .o
b6ld    link          .o   -> a.out
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
| `-nostdlib`   | Do not use the standard library dirs, `crt0.o`, or `-lc`  |

The last stage to run is selected by `-E` (stop after preprocessing), `-S` (stop after code
generation, emit assembly), and `-c` (stop after assembling, emit an object). With none of these,
the objects are linked into an executable.

## Linking

When linking (no `-E`/`-S`/`-c`), `b6cc` invokes `b6ld` with `-e _start` and:

- prepends the **startup object** `crt0.o`, located under `<prefix>/share/besm6/lib` —
  `~/.local/share/besm6/lib` is tried first, then `/usr/local/share/besm6/lib`. A missing
  `crt0.o` is a fatal error;
- adds the standard library search directories (`-L…`) for those same prefixes;
- links the implicit C library with `-lc`.

`-nostdlib` suppresses all of the above for a freestanding link (no `crt0.o`, no lib dirs, no
`-lc`), so a missing `crt0.o` is not an error in that mode.

## Reserved options

`-O` and `-g` are accepted for compatibility but are currently no-ops: no optimizer mapping onto
`b6lower` and no debug-info format have been defined for the BESM-6 toolchain yet.

## Build & test

`b6cc` is built by the top-level `make` (installed as `b6cc`; see the repo
[README](../../README.md) and [CLAUDE.md](../../CLAUDE.md) for the build system). Its end-to-end
test suite is [test/cc_test.cpp](test/cc_test.cpp), run by `make run`.
