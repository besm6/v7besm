# cmd/cc — TODO

Deferred work for the `b6cc` C compiler driver. The driver currently drives the
pipeline `b6cpp → b6parse → b6lower → b6codegen → b6as → b6ld`; `-E` and `-S`
work, but the later stages are not yet usable.

## Blocking the full pipeline

- **`b6as` cannot yet assemble `b6codegen` output.** The Madlen assembly emitted
  by `b6codegen` is not yet compatible with what `b6as` accepts. Until the two
  are reconciled, `-c` (compile to object) and the default compile-and-link path
  will fail at the assemble stage. `-S` (stop after codegen) works.

- **Linking is not wired up.** No startup object (`crt0`) is installed under
  `<prefix>/share/besm6/lib` (only `libc.bin` is present), and the `-lc` search
  path / library-resolution convention for `b6ld` is not defined yet. The
  `link_objects()` step is implemented but expected to fail until this is sorted:
  decide the crt0 object, the library directory, and how `-l` names map to files.

## Reserved / unimplemented options

- **`-O` is accepted but a no-op.** Decide how to map it onto `b6lower`'s
  optimizer (its passes are on by default and can be disabled with `--no-*`);
  possibly gate the optimizer off at `-O0` and on otherwise.

- **`-g` is accepted but a no-op.** No debug-info format is defined for the
  BESM-6 toolchain yet.

## Dropped legacy options

The following options from the original v7 `cc` were removed as obsolete for this
toolchain; re-add if a concrete need appears:

- `-m` (use `m4` instead of `cpp`)
- `-A` / `-a` (skip compiler / skip preprocessor)
- `-p` (profiling; the `mcrt0` startup object)
- `-M` (emit make dependencies)
- `-L` (inline functions)
- `-x` / `-X` (assembler/linker local-symbol flags) — reintroduce as pass-throughs
  if needed.

## Polish

- Add a `README.md` for `b6cc` once the pipeline runs end-to-end.
- Consider passing user `-D`/`-I`/`-U` ordering guarantees and validating the
  besm6 include directory instead of silently omitting it when absent.
