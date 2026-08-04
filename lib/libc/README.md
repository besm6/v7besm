# libc — the Unix v7 C library on the BESM-6

`libc.a` and `crt0.o`: the C library a user program under this kernel links against. 187
objects, **12,464 words**, 147 sources over 7,503 lines. It answers every declaration
[`../../include/`](../../include/)'s hosted half makes — which is C11, not K&R, so a good deal
of it is newer than v7.

Two source trees feed it, and the division is worth knowing before touching anything:

- **Unix v7 libc**, by way of Robert Nordier's x86 port — `csu`, `gen`, `stdio`, `sys`. This is
  the *content*: the algorithms, the v7 semantics, the file-by-file decomposition. Its
  machine-dependent parts were x86 and are rewritten, not ported.
- **The external [c-compiler](https://github.com/besm6/c-compiler/)'s own `libc/besm6/`** — the
  whole `str*`/`mem*` family, `atoi`, and the `doprnt` engine, **taken rather than ported**.
  They were already written for this machine's fat `char *` and one-word scalars, and their
  signatures already match the `<string.h>` this tree supplies, so nothing ends up defined as
  `int n` where the declaration says `size_t`. Each such file says at its head where it came
  from, and a divergence has to be written down there.

The four largest objects are `doprnt.o` (1,435 words), `crypt.o` (1,055), `doscan.o` (860) and
`strftime.o` (818). One function per file, so `b6ranlib`'s index lets `b6ld` pull only what a
program actually calls.

**The kernel uses none of it.** It has its own `printf` in [`../../kernel/prf.c`](../../kernel/prf.c)
and calls no library routine, linking `libruntime.a` alone for the `b$*` compiler-support
helpers.

## Layout

```text
libc/
    CMakeLists.txt  builds libc.a and crt0.o, including the mkstub codegen
    csu/            crt0                                    (1 file)
    sys/            syscall stubs, cerror, sbrk, the exec wrappers  (18)
    gen/            strings, ctype, setjmp, malloc, conversions, <time.h>, directory(3), misc  (71)
    stdio/          FILE machinery, the printf and scanf engines, the accounts  (66)
    man/            the v7 manual pages, sections 2 and 3, and two that are not  (87)
```

**The objects all land flat in one binary directory**, which is not merely a build detail: it is
why `gen/cuexit.c` is not called `exit.c` (it would collide with `sys/exit.S`) and why every
basename in the library is unique. `libc_src()` in [`CMakeLists.txt`](CMakeLists.txt) is the
CMake stand-in for a Makefile's `vpath`, searching `sys gen stdio csu` by basename — and it
must preserve the entry's **real case**, so that on a case-insensitive filesystem a `.S` source
(which needs cpp) is still told from a `.s` (which goes straight to the assembler).

`crt0.o` sits beside the archive rather than in it: a program's startup is named on the link
line, never pulled by symbol, exactly as v7 keeps it in `/lib/crt0.o`.

## Building and linking

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/lib/libc/libc.a and crt0.o among everything else
make install    # puts both in share/besm6/lib, beside the external libruntime.a
make run        # runs the test programs -- see Testing
```

**Link order is a contract, not a style:** ours first.

```sh
b6ld crt0.o prog.o -lc -lruntime -o prog
```

`b6ld` scans each archive exactly once, where it stands, and pulls a member only for a symbol
still undefined. libc calls the `b$*` helpers and no helper calls back — `b6nm libruntime.a`
shows its only undefined names are `b$*` of its own. `b6cc` puts that pair on the line itself.

**One hazard is silent:** a routine missing from `libc.a` can be satisfied from `libruntime.a`
instead of failing to link. The check is to relink *without* it and read the errors — every
undefined name must be a `b$*`:

```sh
b6ld crt0.o prog.o -o /dev/null -L…/libc -lc
```

`install()` preserves the source timestamps, and that matters: `b6ld` calls a randomized archive
out of date when its mtime is newer than the date in `__.SYMDEF`
([`../../cmd/ld/library.c`](../../cmd/ld/library.c)), so copying without `-p` would leave
`b6ranlib`'s index untrusted where it lands.

## `crt0`

[`csu/crt0.s`](csu/crt0.s), ten words. **Nothing is handed over in a register.** Both gates —
`exece()` in [`../../kernel/sys1.c`](../../kernel/sys1.c) and `Machine::exec()` in
[`../../cmd/sim/machine.cpp`](../../cmd/sim/machine.cpp) — lay the argument block at the fixed
base **`070000`** and zero every register but `r15`, whose stack pointer is already seeded and
must not be touched. In order, `crt0`:

1. issues `ntr 7` — the mode word every `b$` helper expects;
2. reads `argc` from `070000`;
3. computes `environ = &block[argc + 2]`, past `argc` itself, the `argc` pointers and the null
   terminating `argv[]`;
4. pushes `argc`, the *address* of the vector, and `environ`, and calls `main`;
5. tail-jumps to `exit` with `main`'s status already in the accumulator, which is where `exit`'s
   one argument belongs.

Two addressing notes it records itself. **`070000` needs no address extension** — a short address
field reaches `[0..07777]` and `[070000..077777]`, the segment bit being worth exactly `+070000`
— so `xta 070000` is one instruction. The **bss** references do need the `< >` escape, because
the segment order is `const|text|data|bss` and bss is the one segment a large program can push
past the 12-bit field.

**Every slot of both vectors strides by one word.** A `char *` is a fat pointer and carries a byte
offset, but a `char **` is a plain word address — which is what `argv` and `envp` are.

## The syscall stubs

**Most leaves are generated.** [`sys/syscalls.tbl`](sys/syscalls.tbl) lists `symbol macro` per
line — 40 of them — and `sys/mkstub` turns each into one `.S` and one object. Adding a syscall is
adding a line and nothing else. The generated stub is four lines:

```
        .globl  open, cerror
open:
        $77     SYS_open
     14 v1m     cerror
     13 uj
```

**No syscall number is written down anywhere under `lib/`.** Column 2 of the table is the `SYS_*`
macro of [`../../include/sys/syscall.h`](../../include/sys/syscall.h), and the stub issues
`$77 SYS_open`; that is what forces the `.S` suffix, since `b6cc` sends a `.S` through the
preprocessor and a `.s` straight to the assembler. Three symbols differ from their macro —
`lseek` issues `SYS_seek`, `_break` issues `SYS_break` (`break` is a keyword), and libc has no
symbol spelled `exece` in the table at all. `SYS_sigret` is the one macro with no stub: the
kernel plants that word itself.

### Twelve are hand-written, and each for a stated reason

| File | Why it cannot be generated |
| --- | --- |
| `exit.S` | no return, so no `13 uj` and nothing to test r14 for |
| `exec.S`, `exece.S` | a successful exec never comes back; reaching the next instruction *is* the failure, so the branch to `cerror` is unconditional |
| `fork.S` | the second result decides which side you are — see below |
| `wait.S`, `pipe.S` | the gate takes no arguments; results arrive in registers |
| `getpid.S`, `getuid.S`, `getgid.S` | one extracode, two entry points, the second result in r12 |
| `time.S` | the gate takes no argument; the store through `tloc` is libc's own doing |
| `dup.S` | the gate's arity (2) disagrees with C's `dup(fd)` (1) |
| `cerror.s` | the shared error tail, which also *defines* `errno` |
| `sbrk.c` | byte↔word conversion; C, not assembly |
| `execl.c`, `execle.c`, `execv.c` | C variadic wrappers over `exec`/`exece` |

### The gate contract

Arguments 1…N−1 sit just below `r15` in direct order, the **last** one is in the accumulator,
the result comes back in the accumulator, the error number in **r14** (zero on success — there
is no carry flag), and a second result, where v7 has one, in **r12**.

- **A stub must not pop the stack, and therefore has no prologue.** The gate stands in for the
  called function and performs the callee's cleanup itself (`tr->r15 -= n - 1` in the kernel,
  the same in `b6sim`). A `b$save` here would move the arguments out from under it.
- **The arity is the C prototype's, and nothing in the stub carries it.** The kernel reads it
  from `sysent[].sy_narg` ([`../../kernel/sysent.c`](../../kernel/sysent.c)) and `b6sim` from
  `syscall_nargs()` ([`../../cmd/sim/syscall.cpp`](../../cmd/sim/syscall.cpp)). Those two and
  every caller must agree: a count that disagrees reads every argument from the wrong slot *and*
  drifts the user stack by a word per call. The table records each prototype in a comment for
  exactly that reason — it is the one thing a reader of the stub cannot see.
- **`errno` cannot be picked up from C.** r14 is caller-saved and the compiler loads it with the
  negative argument count before every call, so any C statement between the extracode and the
  test would have destroyed it. **That is the whole reason `sys/` is assembly and not C.** A
  stub that forgets the `14 v1m cerror` still returns −1 — the gate puts it there — and only
  `errno` gives it away, which is what `test/errno` exists to catch.
- **`cerror`'s −1 must be written inline.** It is −1 as a C `int`, not the 48-bit all-ones that
  `b6as`'s unary minus produces, and an equate would truncate it to 24 bits without a diagnostic
  ([`../../doc/Assembler_Manual.md`](../../doc/Assembler_Manual.md) §5).
- **An extracode returns to the left half of the *next* word.** Whatever is packed after `$77 N`
  in its own word is never executed and no gate can recover it. `b6as` word-aligns after an
  extracode, so a stub may be written as the obvious three instructions; a stub assembled with
  `-a`, or by any other assembler, may not.
- **A second result is only 15 bits.** r12 is an index register, so `wait`'s status — `code << 8`
  — is truncated for any exit code above 127. It bites identically on the kernel and under
  `b6sim`. Widening it means giving `wait` an argument again and writing the status through the
  caller's pointer kernel-side; until then, exit codes stay small.

Three notes the table itself carries, so they are not re-litigated:

* **`lseek` is uniform because `off_t` is one word.** `sysent[19]` said `narg 4`, a PDP-11
  leftover where the offset was a two-word `long`; it says 3 now, matching `seek()`'s own
  argument struct. Same story for `stime`'s `time_t`.
* **`ptrace` needs no `errno = 0` preamble**, which v7's had. −1 is a legal `ptrace` result, and
  on the PDP-11 only the carry flag distinguished it from failure; here r14 is the authority.
* **`signal` is uniform, and stayed uniform when delivery landed**, because the return path is
  not libc's. See below.

## What the machine forced to change

Everything in [`../../doc/`](../../doc/) applies, `Besm6_Data_Representation.md` and
`Besm6_Calling_Conventions.md` above all. What bites *here*:

### A flag packed into a pointer goes above the address, never in bit 0

v7's `malloc` packs its `BUSY` flag into **bit 0** of a block's link, which is free on a
byte-addressed machine because its list cell is two bytes wide there and every block is even.
**Here a block is exactly one word and an address is a word index**, so adjacent blocks differ
by 1 and bit 0 is a *significant* address bit — setting it would name the next block.

The flag moves **above** the address, to **bit 16**: a regular (non-fat) pointer carries its
15-bit word address in bits 15–1 with bits 48–16 zero, so bit 16 is free, one past the top of
the address space, and sits far below the bit-48 marker that would make a marked link look like
a fat `char *`. Nothing else in the algorithm notices — v7 already clears the flag before every
dereference and every comparison — and the casts it takes are free, pointer↔integer being a bare
copy in both directions.

### A null word pointer cast to `char *` is not a zero word

The cast sets the marker and an offset over the zero address. `p == NULL` still answers
correctly — the compiler compares the address part — but `if (!p)` need not. So
[`stdio/setbuf.c`](stdio/setbuf.c) writes `buf == NULL` and never `!buf`, and `realloc`'s two
failure returns are kept apart where v7 writes them as one `return (char *)q`: return a plain
`NULL`, never a cast one.

### `sbrk` fails with `NULL`, not `(char *)-1`

v7's value would have to be fabricated out of an integer, which the fat-pointer rule forbids;
the break can never legitimately be word 0, so `NULL` costs nothing and `malloc` tests for it.
Three more facts live in [`sys/sbrk.c`](sys/sbrk.c):

- **The `break` syscall takes a word address.** A fat `char *` and a plain word address arrive as
  the same 15 bits and the byte offset is *dropped*, so a mid-word pointer would floor the break
  to its word and hand back memory that was never granted. `sbrk` keeps the break as a real
  `char *` and converts the byte increment itself.
- **Rounding is asymmetric on purpose.** Up when growing, toward zero when shrinking: `btow()` is
  `(x + 5) / 6` and C truncates a negative quotient toward zero, so an exact multiple of six
  would come up one word short.
- **Growth past the address space wraps, quietly.** A `char *` carries 15 bits of word address,
  so an increment past the end does not overflow. It is caught by the only symptom it has —
  *growth that did not grow*.

`end` is declared `extern char end[]`, an array, so that its decay produces a genuine fat pointer
at byte offset 0 rather than a cast. **The break is granted a page at a time** — 1024 words, both
kernel and `b6sim` round up — so the allocator's growth chunk and the step it backs off by are
both a page: anything smaller asks for exactly what was just given, or exactly what was just
refused.

### `long` is `int`, one word

So `atol` *is* a call to `atoi`, and `lseek` and `stime` each shed the second word of a `long`.
`sizeof(int) == 6` char-units, `NBPW == 6`, and `sizeof arr / sizeof arr[0]` still counts
elements.

### A signed value cast to unsigned is not widened

An integer is a 41-bit two's-complement field with bits 48–42 **zero**, so the raw word of a
negative value is not its 48-bit two's complement, and the front end *reinterprets* rather than
sign-extends: **`(unsigned long)(-1L)` is 2^41−1, not `ULONG_MAX`**. Three places had to be
written around it:

| Where | The idiom that reads wrong | What is written instead |
| --- | --- | --- |
| `gen/strtol.c` | BSD's `-(unsigned long)LONG_MIN`, and `acc = LONG_MIN` on an unsigned accumulator | every cast value-preserving; the negative cutoff is `(unsigned long)LONG_MAX + 1`, and `LONG_MIN` is returned as the constant — `acc` holds 2^40 there, which no `long` can |
| `gen/calloc.c` | `(size_t)-1` as the overflow bound | the bound written out; `(size_t)-1` is 2^41−1 here, not 2^48−1 |
| `stdio/doprnt.c` | `-(int)ul` for the magnitude of a negative | `2^41 - ul` in unsigned arithmetic |

The `doprnt` one was a live bug: it overflowed the 41-bit signed negate at `LONG_MIN` *and*
mishandled every other negative besides.

### Where an array's neighbour is not where the PDP-11 left it

v7's `crypt` declares `static char L[32], R[32]` and then **indexes past the end of the first**:
the initial permutation runs `L[j]` out to `j == 63`, which worked because the PDP-11 laid the
two adjacent with nothing between. **Six chars pack into a word here**, so a 32-character array
occupies six words with four bytes to spare and `L[32]` is that padding, not `R[0]`. The two are
one array of 64 now, `L` and `R` naming its halves.

**The symptom, if this is ever undone, is not a crash:** `crypt()` goes on producing thirteen
plausible characters, and they are the wrong ones. Which is why `test/pwent` pins six vectors
taken from the **host's** `crypt(3)` and not from this library's own first output.

### An argument list *is* an `argv[]`, and `execl` copies nothing

Arguments are pushed in direct order into one contiguous parameter block, every scalar exactly
one word, and `<stdarg.h>` defines `va_start(ap, last)` as `&last + 1` — so the `va_list`
already points at a null-terminated array of `char *`, which is exactly what the gate reads.
`execl` is three lines. v7 said the same thing as `&args` and could not say it portably.

`execle` walks to the terminator and takes the word after it, and **that terminator must be read
as a raw word** (`va_arg(ap, long)`). A null argv slot is a zero word, but a `char *` is a fat
pointer, so re-reading that zero word *as one* would decorate it into a nonzero value and the
walk would never end. `doprnt`'s `%s` null test and `doscan`'s whole argument carriage are the
same rule: read the word, reinterpret at the point of use.

### A relational between two `char *` — safe, and it was not always

**Inside libc it is safe, and that is not luck.** A fat pointer does not sort as a plain word —
incrementing one *decreases* its 3-bit byte offset, which sits above the word address — but the
compiler lowers a relational between two byte pointers through `b$pdiff`, the same helper as
`-`, and tests the sign. `memmove`'s direction test and `qsort`'s partition both depend on
that, and `test/strings` overlaps *within one word* on purpose to keep it so.

**It only became safe on 2026-06-17.** Before that the relational reduced to an integer
comparison of the whole word, the offset field dominated the address field, and the ordering
came out scrambled and inverted within a word;
[`../libtermcap/README.md`](../libtermcap/README.md) and
[`../libcurses/README.md`](../libcurses/README.md) had four and eleven of these to delete, and
their rewrites stayed. [`../../cmd/README.md`](../../cmd/README.md) §2 is the account.

**One was live in libc**, in [`stdio/getpass.c`](stdio/getpass.c). v7 wrote
`if (p < &pbuf[8]) *p++ = c;`, and `pbuf` is nine characters, so `p` starts at byte #0 of its
first word (offset field 5) while `&pbuf[8]` is byte #2 of the second (offset field 3): **the
test was false on the very first iteration, nothing was ever stored, and `getpass()` returned the
empty string every time.** It is an `int` index now. That file's header has the arithmetic, and
`man/getpass.3` repeats it.

### There is no 16-bit unit, and a word is six bytes

`swab` is rewritten over bytes — v7's loop was over `short *`, and `short` is an alias for `int`
here, so it would have swapped the halves of a *word* and meant nothing to any caller. `getw`
and `putw` move **six** bytes, not the PDP-11's two, most significant first, so a word written by
`putw` reads back byte for byte as that word's own six characters. And `qsort` exchanges a word
at a time when it can — the alignment test being expressible in C, since casting a `char *` to
`int *` discards the byte offset and casting back rebuilds it as byte #0, making the round trip
the identity exactly for a pointer already at byte #0.

**`fread` and `fwrite` move a word at a time too, and for them it matters most.** v7's were
`getc`/`putc` loops (its `stdio/rdwr.c`), which on this machine is a fat-pointer load out of the
stream buffer and a `b$stb` read-modify-write into the caller's, *per byte*; one `unsigned`
assignment moves all six. The catch that `qsort` does not have is that **two** cursors must be
aligned — the caller's buffer and the stream's — and they can be mutually misaligned, in which
case no word move is possible at all and the byte loop is still what runs. The transfer is also
flattened to `size * count` bytes rather than v7's loop over items, because the commonest call
in the tree is `fread(buf, 1, n, f)`: a per-item move would be handed one byte at a time and
could never reach a whole word. `bytes moved / size` is v7's count of complete items, and the
divide happens only on a short transfer. `cmd/strip`'s segment copy is the biggest beneficiary
and did not have to change to get it.

Both are covered by `lib/test/stdiot.c`'s `bulkio()`, which sweeps all thirty-six combinations
of the two cursors' byte offsets — a word path that fired when only one of them was aligned
would corrupt thirty of them and leave the diagonal, which is what a naive test sweeps, looking
healthy.

## `directory(3)` — the one family v7 had no form of

`opendir`, `readdir`, `closedir`, `rewinddir`, `telldir`, `seekdir` and `dirfd`, seven objects in
[`gen/`](gen/) over the private [`gen/dirdesc.h`](gen/dirdesc.h). **v7 had none of this.** Every
caller opened the directory as a file and read raw `struct direct` records out of it, and the
eleven programs in `cmd/` that walk a directory each grew their own reader and their own copy of
the same four mistakes: the free slot `unlink(2)` leaves behind, the name that is not
NUL-terminated when it fills the field, the `strlen` that then runs into the next entry's `d_ino`,
and `DIRENTSZ` re-derived. `cmd/ls` was going to be the twelfth and is the first caller instead;
converting the other eight is [`../../cmd/TODO.md`](../../cmd/TODO.md) task C24.

About **230 words** for a caller that only walks (`opendir` 89, `readdir` 121, `closedir` 15,
`dirfd` 5), 298 for all seven — which is why they are seven objects and not one `directory.c`: a
program that never seeks should not link `seekdir`.

Three decisions in it are about **this machine** rather than about directories:

- **The cursor is an entry index into a `struct direct *`, not a byte offset into a `char *`.**
  4.2BSD keeps `char *dd_buf` with a byte offset and forms `(struct direct *)(dd_buf + dd_loc)`. A
  `char *` is a fat pointer here and that cast **floors** to the containing word — right only
  because `DIRENTSZ` happens to be a multiple of `NBPW`. Indexing an array of `struct direct` does
  the same arithmetic with no cast and no dependence on the coincidence; `malloc` returns byte #0
  of a word, so the array is aligned by construction. `dirdesc.h` asserts the multiple anyway, so
  that retuning `DIRSIZ` is a diagnostic and not silent garbage.
- **`telldir` is the plain byte offset**, and there is no machinery behind it. 4.2BSD returns an
  index into a `malloc`'d list of seek points that `closedir` must free, because a BSD entry is
  variable-length and an offset alone cannot name one. Every entry here is exactly `DIRENTSZ`
  bytes, so the offset *is* the entry — and `seekdir` rounds down to one, which turns a caller's
  arithmetic slip from a straddled read of garbage into defined behaviour.
- **The read buffer is sized from the directory** — as many entries as it holds when opened,
  capped at `DIRPB`, which is exactly one filesystem block so that no entry straddles a refill. A
  fixed one-block buffer would be 512 words for a `/dev` of twenty-five entries, and `_NFILE` is
  20: seventeen open directories would be 8,704 words of a 28,672-word address space.

Two behaviours are the **contract**, not conveniences. `readdir()` **skips `d_ino == 0`**, because
`unlink(2)` empties an entry in place and the hole sits in the middle of a live directory; and it
**terminates the name**, which is the whole reason the library exists. And `opendir()` **refuses a
non-directory** where neither v7 nor 4.2BSD had to: `read(2)` on a regular file succeeds here, so
without that test `readdir()` would hand back file contents reinterpreted as entries, silently.

## stdio

**`FILE` grew two members, and both were free.** `_flag` is an `int` rather than v7's `char`,
because all eight bits of a char were spoken for and line buffering needed a ninth; `_bufsiz` is
new, and `setvbuf` is why — v7 wrote `BUFSIZ` into `_filbuf` and `_flsbuf` outright, because
`setbuf()` was the only way to hand a buffer over and promised one of exactly that size. Neither
costs anything: **a `char` struct member occupies a whole word here anyway** — `struct sgttyb` is
four chars in four words.

**A line-buffered stream is held at `_cnt == 0`.** That is the whole mechanism, and it is why the
`putc` macro shows no sign of the mode: every `putc` misses and lands in `_flsbuf`, which appends
the byte and writes the line out on `'\n'`. Only the slow path pays; the fully buffered fast path
is untouched. `stdout` takes the mode when `isatty(1)`, where v7 went fully *un*buffered and
spent a syscall per character — and `fflush` must **leave the count at zero**, since restoring a
real one would send the next `putc` down the fast path and lose the newline flush.

**BSD's `setbuffer` and `setlinebuf` are here too**, in
[`stdio/setbuffer.c`](stdio/setbuffer.c) — neither C11 nor v7, but `cmd/` sources reach for them
([`cmd/cpp`](../../cmd/cpp/cpp.c) does). `setbuffer` is `setvbuf` with the BSD signature, and the
one difference that matters is that a null buffer or a non-positive size means *unbuffered* rather
than a failed call. `setlinebuf` only turns `_IOLBUF` on, where `setvbuf(iop, NULL, _IOLBF,
BUFSIZ)` would `malloc` 3072 bytes — even for `stdout`, which `_flsbuf` otherwise serves from the
static `_sobuf`. `_flsbuf` picks a buffer lazily when `_base` is null, so a stream that has one
keeps it and one that has none pays nothing until it writes: 512 words of heap is worth that much
care in 28,672 words of address space.

**`_IOSTRG` is what makes `snprintf` count.** `sprintf` and `snprintf` build a v7 `_IOSTRG` stream
over the caller's buffer, and `_flsbuf` *drops* the byte when such a stream fills, leaving the
engine counting characters it could not store — which is exactly C11's return value, obtained for
nothing.

**`open_memstream` reuses `_IOSTRG` rather than avoiding it.** POSIX's growing heap sink
([`stdio/memstream.c`](stdio/memstream.c), neither C11 nor v7; `cmd/cpp` is the first caller) is
the *other* kind of descriptorless stream, and `_IOSTRG` is exactly the bit that says so. That is
what makes `fflush` skip it — correct, there is no kernel to hand anything to — and `fclose` skip
both the `close()` and the `free()`, which is what leaves the buffer in the caller's hands. Both
answers come out of `flsbuf.o`, the object every program that prints anything links, with no new
code. `_IOMEM` is the second bit, saying *which* kind: `sprintf`'s fills a fixed caller buffer and
drops the byte, this one grows. **The reuse has exactly one price, and it is not cosmetic.**
`sprintf`'s `_IOSTRG` stream lives on the stack, so no such stream had ever occupied an `_iob`
slot before; `fclose` nulls `_base` only inside the branch a memory stream skips, and `_endopen`
never touches `_base`, so the slot would go back on the free list still naming the buffer just
handed away — and the next `fopen` onto it would drain the memory stream's stale bytes into its
own file and write on into storage the caller has since freed. `fclose` clears the three pointers
in its `_IOMEM` arm, and `lib/test/stdiot.c` has the case that catches it (`stalezz` for `zz`
without the arm).

Three smaller decisions there follow rules already written down elsewhere in this file. **The
stream is held at `_cnt == 0`**, the line-buffering trick, so every `putc` misses into `_flsbuf`
and `*bufp`/`*sizep` can be republished on *every* byte rather than at flush time — which is a
stronger promise than POSIX asks and is what lets `fflush` stay a no-op. **`_flsbuf` reaches it
through a function pointer**, `_memputc`, armed by `open_memstream`: the `_cleanup` bargain again,
worth 245 words that a program calling only `printf` does not link. And **the caller's two
pointers live in a side table keyed by the `_iob` slot**, `filbuf.c`'s `smallbuf[fileno(iop)]`
pattern — `_file` carries the index biased by `_NFILE`, both so that `sizeof(FILE) == 6` never
turns into a divide on the per-byte path and so that a memory stream's `fileno` can never be a
live descriptor (`NOFILE == _NFILE`). One fact about the machine had to be written into the
failure path: **this `realloc` frees before it allocates** and does not give the block back when
it fails ([`gen/malloc.c`](gen/malloc.c)), so a growth failure cannot keep the old text — it
unpublishes instead, a null `*bufp` and a zero `*sizep` behind `_IOERR`.

**`_IONBF` is spelled `_IOUNBUF`.** v7 used that name for a *bit* in `_flag`; C11 uses it for a
`setvbuf` *mode*, and the two cannot be the same number.

**`doprnt` is not v7's.** v7's was x86 assembly in the port these sources came from, so there was
nothing to port; this is the c-compiler's engine — itself from the FreeBSD kernel `printf` —
retargeted to a `FILE *` sink. Two things it lost and one it gained: **the KOI7 upper-case fold is
gone**, this terminal being ASCII, so `%x`/`%X`, `%e`/`%E` and `%g`/`%G` are three distinct pairs
again and a null `%s` prints `(null)` rather than `(NULL)`; the length modifiers widened to the
C11 set and are **all ignored**, every argument being one word whatever type it is named with;
and `%010.2f` keeps its zero fill, C11's "a precision makes `0` ignored" applying to `diouxX`
alone where the original applied it in the flag parser and disarmed the floating conversions too.

**The v-form is the primitive**, and the variadic entry points are `va_start` wrappers around it.
That is the inverse of v7, where `_doprnt` took a frame pointer and the v-forms did not exist —
`<stdarg.h>` is the only way to walk an argument list here.

**`PWLINE`, not `BUFSIZ`.** `getpwent` and `getgrent` size their line buffers from constants of
their own; v7 wrote `char line[BUFSIZ+1]` where its `BUFSIZ` was 512 bytes, and **`BUFSIZ` is
3072 here** — one disk block, in bytes — so keeping the spelling would have put 513 words of bss
in each of two files. It looks like tidying and is not.

## `exit` reaches `_cleanup()` through a pointer

Its own section, because it is the sharpest measurement in the library.

`crt0` tail-jumps to `exit`, so `cuexit.o` is in **every** program ever linked. Calling
`_cleanup()` outright — which is what v7 does — makes that object reference it, and every program
drags in `flsbuf.o` and behind it `_iob`, the two buffers, `malloc`, `free` and `close`, whether
it ever prints or not. **`hello` measured 2,255 words that way and 100 through the pointer**
(it is 97 today).

**A weak definition does not help and cannot.** `b6ld` pulls an archive member only for a symbol
still *undefined* (`load_ranlib_members()`, [`../../cmd/ld/library.c`](../../cmd/ld/library.c)),
so a weak no-op would satisfy the reference and keep the real `_cleanup` from ever being pulled —
the opposite of what is wanted, and true of weak definitions generally, not just of this linker.
`_flsbuf()` arms the pointer instead, on the first buffered write, the one place every write path
passes through. Reading through a `FILE` arms nothing and needs nothing: there is no unwritten
data behind a read buffer, and the kernel closes the descriptors anyway.

**The same rule binds anything else `exit` ever has to call.** When `atexit` lands this becomes a
special case of it — stdio would register `_cleanup` like any other handler — but a single word is
cheaper than an `atexit` table in every program.

## A signal handler costs libc nothing

Phase 6 added exactly one routine here, `raise`, and changed no other. The kernel builds the
signal frame on the user stack and **plants the return path in it**: a `$77 SYS_sigret` word
(`sigcode`, [`../../kernel/besm6.S`](../../kernel/besm6.S)) that `sendsig()` copies out above the
frame and names in r13, so a handler's ordinary `13 uj` return trips an extracode and `sigret()`
reloads the frame.

The consequences here are all *absences*: the `signal` stub stays the plain generated leaf, there
is no `dvect`/`tvect` trampoline of the kind the x86 port carried, no libc table shadowing the
kernel's `u_signal[]`, and `signal()` therefore still answers with the **true** previous
disposition — including the `SIG_DFL` a caught signal is reset to on delivery.

**It could not have been libc's work anyway:** which *half* of a word to resume at lives in SPSW
and only `выпр` restores it, so a handler interrupted from a right-half instruction can be resumed
only by the kernel. A handler is entered by the ordinary one-argument convention — the number in
the accumulator, r14 = −1, and R = 7, the mode word `crt0` would otherwise have established and a
handler has no opportunity to. See [`../../doc/Unix_Context_Switch.md`](../../doc/Unix_Context_Switch.md) §10a.

## Eleven upstream bugs fixed rather than carried

`getpass`'s is above; the rest, with what each does when it fires:

| Where | The bug | What happens |
| --- | --- | --- |
| [`gen/crypt.c`](gen/crypt.c) | `L[32]`/`R[32]` indexed to 63 | thirteen plausible characters, all wrong |
| [`stdio/getpw.c`](stdio/getpw.c) | two field walks guarded against a `'\n'` that cannot be there | a line with fewer than three colons walks off the caller's buffer |
| [`stdio/getgrent.c`](stdio/getgrent.c) | the `MAXGRP` member vector filled without checking | a long group line walks off a file-scope array into the bss behind it |
| [`gen/getlogin.c`](gen/getlogin.c) | `ubuf.ut_name[8] = ' '` as a sentinel | one past the end of an eight-character field |
| [`gen/ttyslot.c`](gen/ttyslot.c) | the bound tested *after* the store | a 32-character line writes one past the array |
| [`gen/ttyname.c`](gen/ttyname.c) | unbounded `strcat` of a directory entry | overrun on a long entry |
| [`stdio/ungetc.c`](stdio/ungetc.c) | `_ptr` stepped off a **null** base | on the PDP-11 a wild address; here a store to word 0, which the program owns |
| [`stdio/fseek.c`](stdio/fseek.c) | the resolved position left uninitialised | a seek reports a position never computed |
| [`stdio/popen.c`](stdio/popen.c) | a failed `fork` leaks both pipe ends | a program that retries runs out of descriptors |
| [`stdio/doscan.c`](stdio/doscan.c) | `_sctab[getc(iop)]` read before the EOF test, and `%[` walking a `char` subscript into a 128-entry table | `_sctab[-1]`, and out of bounds for any byte above 0177 — reachable, `char` being unsigned here |

## A declaration is not a definition, and nothing here catches that

`<string.h>` has declared **`strerror`**, **`strcoll`** and **`strxfrm`** since the header tree
went C11, and until task C2a *none of the three was defined anywhere*. Nothing noticed, and
nothing could have: [`../test/headers.c`](../test/headers.c) includes every header twice, which
proves a header parses and says nothing about what libc contains, and a declaration with no
definition costs a link error only when somebody calls it. `cmd/kill` was the first caller
`strerror` had ever had.

[`gen/strerror.c`](gen/strerror.c) exists now — it is the bound test `perror` was already doing
open-coded, and `perror` is written in terms of it, so the two cannot name the same `errno`
differently. **`strcoll` and `strxfrm` are still only declarations**; in the C locale they are
`strcmp` and a `strncpy`-plus-length, and the first caller can write them.

The general point is worth keeping: **the header tree is a promise the library has to be
checked against separately.** `lib/test/*.c` is that check, and a routine no test names is a
routine that may not be there.

## What is absent, and why

- **`nlist`** — **not coming**, and the question this used to defer is settled. It was deferred
  on a header problem: a caller needs `struct nlist`, and the `b.out` format is described once
  and under `cross/besm6/`, which guest code cannot reach. The real objection turned out to be
  larger and simpler. **There is no kernel image on the root filesystem** — `root.manifest`
  names no `/unix`, and `kernel/unix.ini` has the *simulator* load one off the build host — so
  `nlist()` would have had nothing to open, and the three programs named as its first callers
  wanted it for exactly the thing it could not have done.

  What replaced it is **`kctl(2)`** (`<sys/kctl.h>`, `kernel/ksym.c`, `doc/Unix_V7_System_Calls.md`
  §2.5): the kernel carries a small table of the variables it publishes, and one call reads it.
  Only `gen/kgetsym.c` is libc's share of that — thirty lines over `KCTL_STAT`, for the callers
  that want an address rather than a value. There is no `struct nlist`, no `<nlist.h>` and no
  `nlist.3`, which also keeps `include/README.md`'s `a.out.h` rule intact: the `b.out` format
  stays described once, under `cross/besm6/`.

  The polarity trap is worth recording since it is why an `nlist`-shaped API was not built over
  `kctl` for source compatibility. RetroBSD's `knlist()` returns the count **found** where
  `nlist()` returns the count **not** found, and its own `fstat` gets the test backwards. Every
  caller here was being rewritten anyway, and all three now are: `cmd/ps`, `cmd/pstat` and
  `cmd/iostat` call `kctl(2)` and `kgetsym(3)` directly and none of them wants an array of
  structures back. So the only thing the old shape preserved was an invitation to that mistake.
- **`atexit`** — see above.
- **`monitor`/`mcount`, and `cc -p`** — no profiling runtime. The `profil` gate exists and has a
  generated stub; nothing calls it.
- **`l3tol`/`ltol3`, `locv`, `nargs`, `fptrap`** — PDP-11 artefacts with no meaning on a 48-bit
  word machine.
- **`regex` (`re_comp`/`re_exec`), `valloc`, `ctermid`, `cuserid`** — not ported, nothing calls
  them.
- **The eleven `<fenv.h>` routines**, declared and not defined. The environment is degenerate and
  honestly so: no sticky exception flags, no rounding-mode register, no arming a floating trap.
- **`stty`, `gtty` and `times` are *not* absent**, and were never the problem they look like: all
  three are real syscalls on this kernel, not v7's `ioctl` write-around, and have been generated
  leaves since the stubs first landed.

**`frexp`, `ldexp` and `modf` are here and not in libm**, because the conversions need them —
`atof`, `ecvt` and the printf engine — and v7 keeps them in `gen/` too. A program gets them
without `-lm`; see [`../libm/README.md`](../libm/README.md).

**Two `<ctype.h>` decisions worth knowing.** `toupper`/`tolower` are *functions*, because v7's
macros are unconditional (`(c) - 'a' + 'A'`) and C11 requires the argument back unchanged when it
is not a character of the other case — which needs the argument evaluated twice, and a macro may
not. v7's unconditional pair keeps the name v7 itself gave it elsewhere: `_toupper`/`_tolower`.
And `isprint(' ')` is **true** now, `_B` occupying the bit v7 left free; the table is otherwise
unchanged, this terminal being ASCII and not KOI7.

## Two compiler defects, since fixed

Both are history, kept for the durable facts they left behind.

**A call through a file-scope function pointer** failed, which forced `qsort` to carry its
comparison in a parameter rather than the file-scope static v7 uses. Fixed upstream, and the
structural change is gone. What remains true: a call through a function pointer is a `wtc` of the
pointer and a bare `13 vjm`, *wherever* the pointer lives — a parameter, an auto, an array element
or a global. `wtc` carries a 15-bit address, so it reaches a global with no `utc` escape, and a
callback needs no special handling at all.

**The comma operator evaluated to its left operand** and never evaluated the right, so
`while (isdigit(c = *p++))` read `"3.5"` as the three digits 3, −2 and 5. Fixed upstream; `atof`
keeps the spelling, and its comment now says that is a preference rather than a workaround.

## Testing

Twenty-eight programs in [`../test/`](../test/) — twenty-two of them libc's, the rest belonging
to libm, libtermcap, libcurses and the kernel's memory driver — and **most of them run twice**:
under `b6sim`
(ctest label `lib`) and off the disk image under the booted kernel (label `kernel`) — from **one
linked image**, staged onto the disk by a copy, so that a difference between the two can only be
the harness and never the compiler. Under `b6sim` every system call is the host's, so a kernel bug
cannot show; the two disagreeing means one of them is wrong. Task 25c's first run of that
arrangement found two bugs, both in code nothing else had exercised.

| Program | What it covers |
| --- | --- |
| `hello` | `argv` as `crt0` finds it |
| `vararg` | the one-word argument |
| `errno` | a failing call and the `cerror` arm |
| `procs` | the syscalls that answer in r12 |
| `sbrkt` | the break |
| `malloct` | word alignment, LIFO reuse, coalescing behind a guard block, 200-block churn across `sbrk` growth, both refusal paths |
| `strings` | the string and memory routines, overlapping within one word on purpose |
| `gen` | the small utilities |
| `strtolt` | value, tail and `errno` at the limits |
| `environ` | the vector `crt0` computes |
| `jmp` | `setjmp`/`longjmp` |
| `headers` | the whole `include/` tree, included twice |
| `stdiot` | the `FILE` machinery through a real file, and `open_memstream` |
| `printft` | every conversion, floats and `LONG_MIN` included |
| `scanft` | scanf and the `atof`/`ecvt`/`gcvt` conversions either side of it |
| `execs` | the exec family — it **execs itself** five times, once per wrapper, reading the stage back out of its own `argv` |
| `spawn` | `system` and `popen` where the shell **cannot** be exec'd (b6sim only) |
| `shellt` | the same pair where it can (image only) |
| `timet` | the whole of `<time.h>`, plus `tell` |
| `pwent` | the accounts, the terminal three and `crypt` |
| `dirt` | `directory(3)` — the free slots skipped, a full-width name terminated, `telldir`/`seekdir`, `rewinddir`, `dirfd`, and `opendir`'s refusal of a plain file (image only) |
| `signals` | the signal frame, `SIG_IGN`, a handler raising a second signal, `alarm`/`pause` and the `EINTR` they answer with *after* the handler has run, and `sleep` |

**An `.expected` file may record only what the *program* does**; nothing host-dependent may reach
it. Four worked instances: `environ` checks `getenv` against the vector it was handed rather than
printing names or counts (`b6sim` passes a whitelisted slice of the host's environment, and
`MAKEFLAGS` alone would make a count differ between `make run` and the same run by hand); `timet`
converts only literal `time_t` values; `spawn` never starts a real shell; and `pwent` prints no
line of `/etc/passwd`, checking instead that every entry the walk yields is found again by name
and by id. The harness captures fd 2 along with fd 1, so `perror`'s output is diffed too.

**Two routines have no test, and neither absence is an oversight.** `abort` raises `SIGABRT` and
leaves it at `SIG_DFL`, and `b6sim` services an uncaught `kill` by killing *its own* process — the
guest pid is the host pid — so a test would take the simulator down with the program and report as
a harness crash rather than a result. `getpass` opens `/dev/tty` and would sit there waiting to be
typed at, which a diff-against-`.expected` harness cannot arrange; that is also why its bug went
unnoticed for as long as it did. **It does have a caller now**, which is not the same thing as a
test but is more than it had: `/bin/login` (kernel task 29b) reads its password through it, and
`kernel/test/login` types a wrong one and then a right one, so both answers are exercised on a
real terminal even though no `.expected` adjudicates the routine itself.

**And the directory routines work under the kernel and not under `b6sim`** — `b6sim`'s `read` is
the host's and refuses a directory descriptor.

**The way that fails is worse than an error, and is the reason `dirt` is image-only.** On the host
`open(2)` and `fstat(2)` on a directory both *succeed*; only `read(2)` refuses. So under the
simulator `opendir()` returns a perfectly good `DIR` and the first `readdir()` returns `NULL`, and
every directory in the world reads as **empty** — in exactly the way an empty one does. No
expectation file can tell those apart, and a `b6sim` case would pass while proving nothing. Say it
out loud wherever it comes up: it also means a `b6sim` run of *any* program that walks a tree
(`ls`, `du`, `find`, `tar c`) proves nothing about the walk.

`ttyname` is the older half of the same story, and it does its own `read()` rather than using the
library — deliberately, since `/bin/login` and `/etc/getty` should not carry `opendir` for one
scan of `/dev`; the file says so. `ttyslot` and `getlogin` stand on it. The failure
paths are what the `b6sim` side covers, and until kernel task 29b that was all any side covered:
`ttyslot` answered 0 everywhere for want of an `/etc/ttys` to count. That file is on the image
now, so the two harnesses disagree — which is why the positive answers moved out of
[`../test/pwent.c`](../test/pwent.c), which adjudicates both worlds against one expectation, and
into `../test/ttyt.c`, which runs on the image alone.

## The manual pages

[`man/`](man/) holds **86** of v7's own — 43 from section 2 and 43 from section 3 — **corrected in
place** on the precedent [`../libtermcap/termcap.3`](../libtermcap/termcap.3) set. Every SYNOPSIS
is ANSI, since `b6parse` accepts nothing else; every claim that this port does not honour is fixed
where it stands and marked **Note:**, saying what v7 did as well as what happens now; and the C11
routines v7 had none of are folded into the page that owns them, so `string.3` names the `mem*`
family, `atof.3` names `strtol`, `fopen.3s` names `tmpfile` and `remove`, and so on.

Two edits were structural rather than corrective. Every **`.SH ASSEMBLER`** section is replaced:
v7's gave a decimal call number and a PDP-11 `sys name; arg` line (with an Interdata sequence in
`intro.2`), and neither is how a call is made here — `intro.2` now carries the whole `$77` contract
instead, and each page points at it. And four pages pulled a header in with **`.so
/usr/include/…`**; there is no `/usr/include` on this system, so `stat.2`, `time.2`, `getpwent.3`
and `getgrent.3` have their structures written out.

Six pages that are **not** here: `curses.3` and `termlib.3` belong to the libraries that own them,
`dbm.3x`/`mp.3x`/`plot.3x`/`pkopen.3` are not libc, and `l3tol.3`, `monitor.3` and `nlist.3`
describe routines this library does not have — `nlist.3` permanently, since `kctl(2)` is what
this system has instead (see *What is absent, and why*). Four of section 2 are gone for the same
reason — `indir`, `mpx`, `mpxcall` and `pkon` are not syscalls here.

**Two pages are not v7's at all.** `kctl.2` documents this port's own system call and the
`kgetsym(3)` shorthand over it. `directory.3` documents `opendir`, `readdir`, `closedir`,
`rewinddir`, `telldir`, `seekdir` and `dirfd` — **4.2BSD's library, which v7 had no form of at
all**: every v7 caller opened the directory as a file and read raw `struct direct` records out of
it. Neither page had an upstream original to be edited from, and both carry a `.\"` line at the
top saying so.

**Nothing installs them** — no `CMakeLists.txt` in this tree has a man rule yet, which is also true
of libtermcap's and libcurses' — so they are read with `nroff -man man/malloc.3`.
