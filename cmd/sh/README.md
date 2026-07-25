# sh — the Unix v7 Bourne shell for the BESM-6

`/bin/sh`: S. R. Bourne's shell, compiled by the `b6*` toolchain and staged as
**`build/rootfs/bin/sh`** — the shell `/etc/init` execs to bring the system up single-user
([`../init/README.md`](../init/README.md), [`../../kernel/TODO.md`](../../kernel/TODO.md) task 24).

Like [`cmd/init/`](../init/), this is a `cmd/` subdirectory that is **not a host tool**. Nothing
here runs on the build machine.

## Building

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/rootfs/bin/sh among everything else
make run        # runs its size check and its five b6sim tests, with the rest
```

Two conditions gate it, both shared with `kernel/` and `lib/`: the external
[c-compiler](https://github.com/besm6/c-compiler/) must be installed, and libc must build, since
the link is `crt0.o *.o -lc -lruntime` in that order. [`CMakeLists.txt`](CMakeLists.txt) is one
`b6_prog()` call plus `-I.`, which matters: `defs.h` includes `"ctype.h"`, and that must resolve
to the shell's own character tables rather than the C11 `<ctype.h>` in the system tree.

It uses **7,648 words of the 28,672** available, with the highest relocatable symbol at word
7,656 of the 32,767 a 15-bit pointer reaches.

## What the port changed

The sources are v7's, and the shell they build is v7's. Two separate kinds of change were needed.

### C11

`b6parse` is strict C11: no implicit `int`, no K&R parameter lists, no untyped `register i;`. So
every function has a prototype and an explicit return type, and the file-local ones are `static`.

**The ALGOL-68 macro dialect is gone.** `mac.h` defined `IF`/`THEN`/`ELSE`/`FI`,
`SWITCH`/`IN`/`ENDSW`, `REP`/`PER`/`DONE`, `LOOP`/`POOL`, `BEGIN`/`END`, `ANDF`/`ORF`/`NEQ` —
Bourne wrote the shell in it, and it preprocesses to perfectly legal C11. It was removed anyway,
because the pointer work below has to be read and reviewed and cannot be while the control flow is
spelled in macros clang-format will not format. Only the value macros survived, into `defs.h`.

Three things that the rewrite could have broken silently, and did not:

* **`VOID` was a typedef for `int`, not for `void`.** Exactly one function relies on it:
  `cmd.c`'s `skipnl()` is declared `LOCAL VOID` and its *returned value* decides whether
  `for x in a b c` parses as a list or as a bare `for x`. It is `INT` now.
* **Every `switch` has a deliberate fall-through**, and `SWITCH`/`IN`/`ENDSW` hid them all. The
  important one is `case TCOM` falling into `case TFORK` in [`xec.c`](xec.c) — that is how every
  external command gets run. Each one is marked in the source.
* **`BOOL` must stay an integer type.** The name says boolean, but `trapnote`, `trapflg[]` and
  `exfile()`'s `prof` parameter all carry multi-bit flags; `exfile(ttyflg)` ORs 040 straight into
  `flags`. Under `bool` that becomes 1 == `noexec`, and the shell would parse everything and
  execute nothing with no diagnostic.

`defs.h` no longer **defines** the shell's globals. v7 declared some sixty of them in that header
— `INT flags;`, with no initializer and no `extern` — and let the linker merge the twenty
copies into one common block. C11 has no common block, so they are `extern` there and defined once
in the new [`glob.c`](glob.c). Two were defined *twice* in v7 (`nosubst`, in `io.c` and
`service.c`) and the expression stack's four pointers were declared in `stak.h` and defined
nowhere at all.

Three declarations the header tree did not have, added the way `cmd/init` added `kill()` and
`<sys/wait.h>`: **`ioctl()`, `stty()` and `gtty()`** in [`<sgtty.h>`](../../include/sgtty.h) — the
header exists for those calls and declared none of them, so `lib/libc/gen/isatty.c` still writes
its own `extern` — and **`times()`** in [`<sys/times.h>`](../../include/sys/times.h), guarded
against the kernel's same-named handler exactly as `<sys/stat.h>`'s block is.

And `typedef struct sysnod SYSTAB[]` had to go: an array of an incomplete type is a C11
constraint violation, and `struct sysnod` is not defined until further down `mode.h`. The two
keyword tables are `SYSNOD[]`.

### The machine

A word is 48 bits and the machine is word-addressed. `sizeof(int) == 6` char-units — one word —
`char` is unsigned, `char *` and `void *` are **fat pointers** (a bit-48 marker, a byte offset, a
15-bit word address), and every other pointer is a bare word address in bits 15–1
([`doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md) §7–8). The v7 shell
is written for a machine on which an `int` and a `char *` are the same thing, so this reaches
further into it than into `init`.

**A flag packed into a pointer goes in bit 16, never in bit 0.** Bit 0 of a word address names the
neighbouring *word*. Two places did it: `blok.c`'s `BUSY` and `service.c`'s `ARGMK`.
[`lib/libc/gen/malloc.c`](../../lib/libc/gen/malloc.c) is v7's *same* allocator with the same fix
already made, and carries the full account; this port follows it rather than restating it. v7's
`Lcheat`/`Rcheat` puns existed only to do this and are gone from `mode.h`.

**Rounding to a word is not a bit mask.** `BYTESPERWORD` is 6, so `& ~(6-1)` is not a rounding
operation at all — it rounds 7 to 8. v7's `round()` is two macros in `defs.h`: `sizeup()` for byte
counts, and `wordup()` for pointers, which works *because* the `char *` → `int *` cast floors, so
adding 5 and flooring is exactly a ceiling. `stak.c`'s `brkincr` grew in steps of 256 in v7, into
768 and 1280 and 1792 — not powers of two, so that mask was already wrong on the PDP-11.

**Every cast from `char *` to a node pointer floors.** It drops the byte offset and keeps the
word; it does not round. The whole parse tree and argument list is built by casting stack
addresses to `ARGPTR`/`COMPTR`/`STRING *`, so the port depends on an invariant that `defs.h` now
states outright: **`stakbot`, `staktop` after `endstak()` or `getstak()`, and every `shalloc()`
result sit at byte #0 of a word.** `endstak()` is the line that keeps it.

**An offset is not a pointer.** `relstak()` yields a stack offset, and v7 stored it in a `STRING`
at three sites, because a byte offset and a `char *` are the same sixteen bits on a PDP-11. Here
an integer cast into a `char *` is not a pointer at all. `absstak()` takes an `INT`.

Smaller ones: `addblok()` added a byte count to the integer value of a word pointer, and built its
end sentinel out of `end+1` — one byte past `end`, which was below `bloktop` and had bit 0 set,
and neither property survives here. `sbrk()` reports failure as `NULL`, not `(char *)-1`, so
`setbrk()` returns a plain flag and nobody has to cast a break address to an `int`. `struct
direct` is four words with `DIRSIZ` 18, not v7's sixteen bytes with `DIRSIZ` 14, and
[`expand.c`](expand.c) hardcoded both. `execve` is spelled `exece`. `itos()` printed at most five
digits and turned a negative into a very large positive; an `INT` here holds a 41-bit signed
value. `prc()`'s parameter must stay a `char`: `&c` on a standalone `char` is a fat pointer at
byte #5, and widening it to `int` would make every `prc()` write a NUL. And `CPYSIZ`, the amount
`subst()` buffers, was 512 because that was one PDP-11 disk block; here a block is `BSIZE`, 3,072
bytes, and 512 is not even a whole number of words — the same conversion `<stdio.h>` makes for
`BUFSIZ`, and it is written as `BSIZE` now rather than as a number.

Names that collided with libc: the arena's `alloc`/`free` (v7 `#define`d them to `malloc`/`free`,
so the shell *defined* both) are `shalloc`/`shfree`; `getenv`/`setenv` are `readenv`/`shenv`;
`io.c`'s two-integer `rename` is `shrename`; `FILE`, `BUFSIZ` and `EOF` are `SHFILE`, `SHBUFSIZ`
and `SHEOF`, since `<stdio.h>` spells the last two 3072 and −1 and either would be silent.

### Two changes of substance

Everything above preserves what the shell does. These two do not, and both are about running out
of room in a 28-page address space:

* **`gmatch()` recursed once per character** of an unbounded pattern. The user stack is four
  pages, and a pattern of a few hundred characters would have run off it with no diagnostic. Its
  three tail calls are loop iterations now; the fourth, in `*`, is not a tail call and stays,
  bounded by the number of stars.
* **`chkstak()`.** `locstak()` guarantees one increment of headroom when a stack item is
  *started*, and the loops that then fill it push as many characters as the user's data has, with
  no further check. v7 got away with that by arrangement: running off the break raises a memory
  fault, and `fault()` extends the arena and lets the faulting store re-execute. That arrangement
  is kept — but it needs the kernel to restart the faulting instruction, which nothing in this
  port has demonstrated yet, and under `b6sim` there is no signal delivery at all. So every
  unbounded push tests first. `test/nospace.sh` is what proves it: without the test that script
  walks out of the address space, and with it the shell says `no space`.

## Tests

Five, under `b6sim`, run by `make run` (ctest labels `sh` and `rootfs`):

| test | what it covers |
|---|---|
| `sh_smoke` | sourcing with `.`, assignment, `${-}`/`${+}`/`${=}` substitution, quoting, positional parameters and `shift`, `if`/`for`/`case`/`while`/`until`, `export`/`readonly`/`umask`/`trap`/`set`, and the exit status |
| `sh_syntax` | the parser's error path — a truncated `if` |
| `sh_heredoc` | here-documents: `copy()` and `subst()`, a quoted terminator, and a document longer than `CPYSIZ` so the flush boundary is crossed — and with them `fork`, a subshell and file redirection |
| `sh_script` | running another shell script: arguments, PATH search, exit status, and a child that forks in turn |
| `sh_nospace` | the arena and the break, to exhaustion |

`b6sim` runs one BESM-6 `a.out` and services its syscalls on the host, which is enough for the
lexer, the parser, macro expansion, the name tree and — underneath all of them — the arena and
the expression stack, which is where this port's pointer work lives.

`fork`, pipes and file redirection **are** reachable, which is not obvious and was worth
finding: v7's shell refuses to redirect a built-in (`illegal io` in [`xec.c`](xec.c)), but a
`( )` **subshell** may be redirected and the built-ins inside it still print. `sh_heredoc` is
built on that, and it is what covers `copy()`, `subst()`, `initio()` and the fork path.

**So is running another shell script**, which is the whole of `sh_script`. It is worth being
precise about how, because it is not what a modern reader expects:

> **There is no `#!` on this system.** The shebang is a 4.xBSD invention; v7 had none, and
> neither this kernel's `getxfile()` ([`kernel/sys1.c`](../../kernel/sys1.c)) nor `b6sim`
> implements it. What runs a script is the **shell**: `exec` comes back `ENOEXEC` — the file
> exists and is readable but is not a binary — and `execs()` in [`service.c`](service.c) takes
> that as "this is a script", points its own input at the file and `longjmp`s back to the top of
> `main()`. The forked child *becomes* the shell that runs it.
>
> A `#!/bin/sh` line is therefore not a comment and not magic: it is a command, it is not found,
> and it prints one spurious error per run before the script carries on. Do not put one in a
> script for this system.

Making that work needed a fix in `b6sim`, not in the shell: `sys_exec()` was reporting `ENOENT`
for *every* exec failure, including "not a BESM-6 a.out". A shell told `ENOENT` concludes the
file does not exist and reports "not found", so no script could ever run — while the kernel,
which gets it right, would have run it. `Machine::ExecError` now carries the errno.

**What is still out of reach**, and why:

* **Executing a compiled program.** There is no external BESM-6 binary on the image yet, so a
  command name that is not a script only ever reaches "not found".
* **Globbing.** [`expand.c`](expand.c) reads directories with `read()` and parses `struct direct`;
  under `b6sim` that is a host directory in a host format, so the read fails and the pattern is
  left literal.
* **Traps and interrupts.** `b6sim`'s `signal()` implements only `SIG_DFL` and `SIG_IGN`.

All three wait for the real kernel under SIMH — `kernel/TODO.md`, task 25 — which is also where
the memory-fault path above gets its first real exercise.

Two notes on the fixtures. They contain **no `#` comments**: the v7 shell has none (they arrived
with System III), so a `#` line is a command. What stands in for one is a `:` line — but its
words are still *parsed*, so a stray backquote in one starts a command substitution that runs to
end of file, and a parenthesis is a syntax error. And the runner clears the environment with
`env -i`,
because `b6sim` hands the guest a whitelist of the host's variables, the shell reads all of them
into its name tree at startup, and `set` prints the tree.

## Known limits

* **`wait()`'s status comes back in r12, a 15-bit index register**, so an exit code of 128 or more
  arrives truncated ([`lib/libc/sys/wait.S`](../../lib/libc/sys/wait.S)). That is the kernel ABI,
  not something `cmd/sh` can repair, and it bites twice: `await()` builds `$?` for a
  signal-killed child as `0200|sig`, which is itself in the truncated range.
* **Most `TIOC*` ioctls land in `nullioctl`** until there is a terminal multiplexer driver
  (`kernel/TODO.md`, task 29).
* **It is not on the disk image yet.** `kernel/test/root.manifest` still names the task-23
  stand-in `kernel/test/coninit.S` as `/etc/init`; pointing it at the real `init` and this shell
  is the last step of task 24/25, and it changes `kernel/test/console`, which asserts on
  `coninit`'s echo behaviour.

`sh.1` is the v7 manual page, kept as it was.
