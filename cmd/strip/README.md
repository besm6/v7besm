# b6strip — remove the symbol table from a BESM-6 `a.out`

Drops the symbol table and the relocation records from an object file or executable, rewriting
each file **in place**: `strip file ...`. What survives is the header — with `a_syms` zeroed and
`RELFLG` set, which is what marks a file as having no relocation records — and the const, text
and data segments behind it.

There is no `-o`: the file named is the file rewritten, which is v7's design and the reason
this program's test compares two copies of one input rather than two outputs of one run.

The on-disk format is [`cross/besm6/b.out.h`](../../cross/besm6/b.out.h), read and written
through [`cmd/libaout`](../libaout). Build with `make`; the engine is covered by the GoogleTest
suite in [`test/`](test), which links it in-process.

## Source layout

| File | Responsibility |
| --- | --- |
| `strip.c` | The whole engine: `copy()`, the chunked byte mover; `strip_file()`, which rewrites one object through the scratch stream; and `strip_run()`, which opens that stream and walks `argv`. |
| `main.c` | The thin command-line wrapper: hands `argv` to `strip_run()` and forwards its exit code. |
| `strip.h` | `strip_run()`, the one entry point, so the tests can call it repeatedly in one process. |

**Three exit statuses, and they are not interchangeable.** 0 is success — including "already
stripped", which is a diagnostic and not a failure; 1 is a soft error, and the file was *not*
touched; 2 is a hard error and the file **may have been clobbered**, which is why `strip_run()`
stops at the first one rather than going on to the next argument. `SIGHUP`, `SIGINT` and
`SIGQUIT` are ignored for the window in which the file is truncated.

## Building for the BESM-6

These same sources — plus the `cmd/libaout` files `strip` calls — are built a **second** time,
by the `b6*` cross toolchain, into `build/rootfs/usr/bin/strip`: task **C9c** in
[../TODO.md](../TODO.md), with [`../nm`](../nm), [`../size`](../size) and
[`../disasm`](../disasm). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the build
machinery, and there is **no second copy of any source**.

**This is the one of the four that writes**, so it is also the one that wanted the target's
filesystem rather than only its address space: a writable `/tmp`, which
[../../root.manifest](../../root.manifest) has.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `BUFSZ` (copy chunk, bytes) | 8192 | `BUFSIZ` (3072) | it is a **stack** array; 8,192 bytes is 1,366 words of a 4,096-word stack |

**The chunk is on the stack, which is the whole of the change.** `copy()` declares `char
buf[BUFSZ]` as an automatic, and nothing on this machine checks the stack — the four pages at
`070000` are simply there, and running off them is silent ([../README.md](../README.md) §6).
At the host's 8,192 bytes that frame is **1,382 words**; at `BUFSIZ`, which is 3,072 bytes and
so 512 words, it is **528**. 3,072 is not an arbitrary cut: it is this machine's block size
(`BSIZE`) and one stdio buffer, so the chunk and the buffer under it move in step.

**`mkstemp()` does not exist in this libc** — only v7's `mktemp()`, which fills a *writable*
buffer and walks one letter, not six. `strip_run()` uses **`tmpfile()`** instead, in both
builds and not behind an `#ifdef`: only the stream was ever wanted here, never the name, and
`tmpfile()` unlinks the file the moment the stream holds it, so the scratch goes away on the
`fclose()` and on an uncaught signal alike. That deleted the `unlink()` at the end and the
`close()` in the error path with it. [`../ld`](../ld) made the same move for the same reason.

The program is **5,501 words** (96 const, 4,163 text, 205 data, 1,037 bss) with its top
relocatable symbol at 5,509 — against ceilings of 28,672 and 32,767.

### The stack

No recursion. `main` → `strip_run` (37 words) → `strip_file` (63) → `copy` (**528**) is **628
words** of the 4,096, and the chunk is five sixths of it. With `BUFSZ` left at the host's value
that same path would be 1,482 — not fatal on its own, but a third of the stack spent on a
buffer that never needed to be there, in a program that also calls `fprintf` (whose `_doprnt`
is 281 words) on every error path.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_strip_obj`** — the host `b6strip` and the native `strip` each over their own copy of
  one object, the two results compared **byte for byte**. The fixture's const block is 600 words
  — 3,600 bytes — on purpose: that is *two* chunks on the target and *one* on the host, so the
  two builds go round the copy loop a different number of times and must still produce
  identical files. That is the case for `BUFSZ` differing at all, and it is the only case here
  that could catch a chunk-boundary bug.
- **`cmd_strip_noargs`, `_notfound`** — ordinary `b6sim` cases with a checked-in `.expected`:
  the usage summary and a missing file, both with status 1. There is no case for a file that
  exists but is not an `a.out`, because every way of naming one puts an absolute build-machine
  path into the diagnostic; `test/`'s `Strip.NotAnObject` covers that arm on the host.

`rootfs_strip_size` (registered by `b6_prog()`) is the third: it holds the program under the two
address-space ceilings. It cannot see the stack, which is why `copy`'s frame is read out of
`build/cmd/strip/rootfs/strip.dis` above rather than estimated.
