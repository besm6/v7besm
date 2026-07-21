# BESM-6 a.out Simulator Manual

This manual describes **`b6sim`**, the BESM-6 program simulator in [`cmd/sim/`](../cmd/sim/).
It is written for a programmer who knows C and Unix but is meeting the BESM-6 for the first
time. It explains what `b6sim` is, why the project needs it, and how to use it to build, run,
and debug BESM-6 programs — which is how the C compiler, the runtime library, and other Unix
software for this port get tested.

Companion references, when you need the details underneath:

- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — opcodes, registers, encoding.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the C calling ABI.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — how C types sit in a word.
- [Assembler_Manual.md](Assembler_Manual.md) and [Linker_Manual.md](Linker_Manual.md) — the
  `b6as` and `b6ld` tools that produce the executables `b6sim` runs.

---

## 1. What `b6sim` is

`b6sim` runs a single BESM-6 program on your ordinary computer. It loads a BESM-6 `a.out`
executable, interprets its instructions one by one on a software model of the CPU and memory,
and — whenever the program asks the operating system to do something (read a file, write to
the screen, exit) — carries that request out on the **host** machine you are sitting at.

It is a **user-level** simulator: it models one running process, not a whole machine. There
is no operating system inside it, no disks, no terminals, no other users — just your program,
memory, and a translation layer that turns the program's Unix v7 system calls into calls on
the host. This is the same idea as Warren Toomey's `apout` for the PDP-11: run the binary,
emulate the syscalls, skip the rest of the hardware.

That is deliberately *different* from the authentic full-machine BESM-6 emulator,
[SIMH](Simh_Simulator.md), which reproduces the real hardware in detail and boots a real
operating-system image — and which is where this project's Unix kernel will ultimately run. Use
SIMH when you want to run the whole OS; use `b6sim` when you just want to run and check one
program quickly. Both load the same `a.out` executables, so a program can be moved from one to
the other unchanged.

## 2. Why the project needs it

This repository is porting Unix v7 to the BESM-6 and, along the way, building the toolchain
to compile C for it: a compiler (`b6cc`), an assembler (`b6as`), and a linker (`b6ld`). All
of that machinery is worthless until you can **run** what it produces and see whether the
answer is right.

`b6sim` is that missing piece. It is currently the only tool in the project that actually
*executes* BESM-6 code. With it you can:

- **Verify the C compiler back-end** — compile (or hand-write) a small program, run it, and
  check that the result matches what the same C prints on a normal machine.
- **Develop the runtime library and a future libc** — the low-level routines that wrap system
  calls (`write`, `open`, `exit`, …) can be exercised directly, syscall by syscall.
- **Test ordinary Unix programs** — anything that talks to the world only through v7 system
  calls can be run and observed.

Because it runs on the host and uses the host filesystem, the edit → assemble → link → run →
inspect loop takes a fraction of a second. No hardware and no OS image are required.

## 3. How it works

A few facts about the machine explain everything `b6sim` does. (Full detail in
[Besm6_Data_Representation.md](Besm6_Data_Representation.md).)

- **The BESM-6 is word-addressed with 48-bit words.** The smallest addressable unit is a
  whole 48-bit word — there are no byte load/store instructions. An address is a *word*
  index, and the address space is 32768 words.
- **One C scalar is one word.** `CHAR_BIT` is 8, but six characters pack into a word, so
  `sizeof(int) == 6` (six char-units) and every `int`, `long`, pointer, `time_t`, or
  `off_t` occupies exactly one word. This is why the simulator's syscall layer is so simple:
  there is no high-word/low-word splitting the way there is on a 16-bit PDP-11.
- **A character pointer is a "fat pointer".** Since memory is addressed by word, a `char *`
  has to carry both a word address *and* which of the six bytes inside that word it points
  at. It does so in a single word: the byte-offset in the upper bits and the word address in
  the low 15 bits.

`b6sim` keeps a model of the CPU (the accumulator `ACC`, index/modifier registers `r1`–`r15`,
the program counter) and an array of 48-bit words for memory. It fetches each 24-bit
instruction, decodes it, and updates that state — a plain interpreter loop.

The one special instruction is the **system-call trap**. In BESM-6 assembly a raw *extracode*
is written `$77 N`. Extracode `077` is the only one valid in a user program, and `b6sim`
treats it as "do Unix v7 system call number `N`". Control leaves the interpreter, the request
is served on the host, the result is placed back in the machine's registers, and execution
continues. (The syscall numbers are the classic v7 ones from
[`kernel/sysent.c`](../kernel/sysent.c).)

The trap need not be written in assembly any more: `$77 N` is what the C compiler emits for
`__besm6_extracode(077, N, acc)`, one of the `<besm6.h>` intrinsics
([Intrinsics.md](Intrinsics.md)). That is how a syscall leaf gets written in C.

The calling convention for a syscall is the ordinary BESM-6 C convention
([Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)): for a call with *N*
arguments, arguments 1…*N*−1 sit on the stack just below the stack pointer `r15`, the **last**
argument is left in the accumulator, the **result** comes back in the accumulator, and **`errno`** (0 on success) comes back in `r14`.
Note that for syscalls `r14` does not have to hold the argument count as a negative number.
Value of `r14` is ignored on input to syscalls.

Five calls have a **second result**, which v7 returned in the PDP-11's `r1` and which comes back
here in **`r12`**: `pipe` (read end in the accumulator, write end in `r12`), `wait` (pid, status),
`fork` (the other side's pid, and 1 in the child / 0 in the parent), `getpid` (pid, ppid),
`getuid` (real, effective) and `getgid` (real, effective). Because `pipe` and `wait` deliver
their second value in a register rather than through a user buffer, they take **no arguments**
at all here, unlike their v7 C prototypes.

`r12` and not `r13`: `r13` is the ABI's return-address register and belongs to the caller. The
BESM-6 Unix kernel uses the same slot — `R_VAL2` in
[`include/sys/reg.h`](../include/sys/reg.h) — so one binary runs on both. Note that `r12` is an
index register and therefore **15 bits**: a second result above 32767 is truncated. Nothing a
v7 guest produces comes close, but a *host* pid can, so `getpid`/`fork`'s second value may not
match the host's `getppid()` on a modern system. The first result is unaffected — the
accumulator is a full word.

The `$77 N` trap stands in for the called function, so it also performs the callee's stack
cleanup: on return it decrements `r15` by *N*−1 — the number of arguments the caller pushed
below `r15` (the *N*th argument travelled in the accumulator and was never pushed). Calls with
0 or 1 arguments leave `r15` unchanged. Because `r14` is ignored, `b6sim` derives *N* from the
syscall number itself (each v7 syscall has a fixed arity). This means a naked syscall stub —
just `$77 N` followed by `13 uj`, as in [`putch.s`](../cmd/sim/tmp/putch.s) — needs no
`c/save`/`c/ret` frame to keep the caller's stack balanced. It does, however, have to place the
extracode carefully:

> **The half-word after `$77` is discarded.**
> An extracode vectors to `0550`–`0577` and its return register ERET holds a **word** address
> (`PC + 1`) with no right-instruction indicator, so `выпр` always resumes at the **left half of
> the next word** — whichever half the extracode itself was in — and anything packed after it in
> the same word is never executed. `b6sim` models this. See
> [Dubna_Context_Switch.md](Dubna_Context_Switch.md) §9 and
> [Unix_Context_Switch.md](Unix_Context_Switch.md) §8.
>
> This is a property of the hardware, not of the simulator, so it constrains every caller:
> **do not pack a live instruction after `$77 N` in its word.** The assembler packs two
> instructions per word, left half first, and a label forces a word boundary — so what decides
> the half is the instruction count since that boundary. An odd count leaves the extracode in
> the right half, where nothing can follow it. When the count does not work out, a `10 utm 0`
> no-op before the extracode pushes it there; that is what
> [`kernel/test/crt0s.S`](../kernel/test/crt0s.S) does before each of its `$77`s. A bare label
> will *not* do the job: text padding is `utc 0`, which loads M16 rather than doing nothing.
>
> The stub referred to above is the shape that gets this wrong —
>
> ```asm
> putch:  $77 4           // LEFT half
>      13 uj              // RIGHT half -- LOST
> ```
>
> — and falls straight through its own return, on the real machine and under `b6sim` alike.

### How a program starts

A program begins at its entry point with a usable stack and its arguments already in memory.
The stack region is the top of the address space: `r15` starts at `070000` and grows **upward**
towards the guard word `077777`, while the heap break starts on the first page boundary above
the bss and grows up towards it from below.

The arguments sit at the **base of the stack**, in a block `b6sim` lays down exactly as the
kernel's `exece()` does ([`kernel/sys1.c`](../kernel/sys1.c)) — so one `crt0` serves the
simulator and the real machine:

```
070000  argc
        argv[0] .. argv[argc-1]     char * fat pointers
        0
        envp[0] .. envp[ne-1]
        0
        the strings, byte-packed six to a word, contiguous
        0                           a closing word; the cursor is rounded up to reach it
  r15 = the first free word above the block
```

Three things about it are worth spelling out:

- **`argc` is always at absolute `070000`.** Nothing is handed over in a register — the
  accumulator and `r1`–`r14` are zero at the entry point, matching the kernel's `setregs()` — so
  a `crt0` needs no hand-off at all: it reads `argc` from the fixed address and derives
  `argv` = `070001` and `envp` = `070001 + argc + 1` from it.
- **The pointer slots stride by one word; only the strings are byte-granular.** They are packed
  contiguously and are *not* re-aligned between one and the next, so all but `argv[0]` normally
  begin mid-word and their fat pointers carry a non-zero byte offset (bits 47–45, field 5 naming
  byte #0). A `crt0` must store what it finds, not rebuild it.
- **The block is below `r15`, so the program's own stack growth can never walk back over it**,
  and the heap cannot grow into it either: `break` refuses to cross `070000`.

With no arguments and no environment the block is still four words — `argc` = 0, the two
vectors' terminating nulls, and the closing word — and `r15` is `070004`.

The same block is built for the initial program load and by `exec`/`exece`: they are one code
path. There is not yet a `crt0` or `libc` in the project to read it, which is what
[`lib/README.md`](../lib/README.md) phase 1 builds.

## 4. Building and installing

`b6sim` builds as part of the top-level toolchain. From the repository root:

```sh
make            # configure and build every cmd/ tool into build/
make install    # install the tools (as b6sim, b6as, b6ld, …) into ~/.local/bin
```

After `make`, the binary is `build/cmd/sim/b6sim`; after `make install` it is on your `PATH`
as `b6sim`. (See the top-level [README.md](../README.md) for the full build story.)

## 5. Command line

```
b6sim [options...] program [arguments...]
```

`program` is a BESM-6 `a.out` executable — the output of `b6ld`. Exactly one is required.

**Option parsing stops at `program`.** Everything after it belongs to the simulated program and
reaches it as `argv[1]` and up, options included — `b6sim -d prog -d` traces `b6sim` and passes
the second `-d` to `prog`. The program file itself, spelled exactly as you typed it, is `argv[0]`.

The program's environment is *not* the host's. `b6sim` passes a fixed short list of variables,
in this order and only when the host has them set: `LANG`, `LC_ALL`, `TERM`, `SHELL`, `PATH`,
`HOME`, `USER`, `LOGNAME`, `TMPDIR`, `EDITOR`, `PAGER`, `MAKEFLAGS`. A v7 program has no use for
the hundreds of variables a modern shell exports, and the block competes for the few thousand
words between `070000` and the guard word. Values are passed through verbatim, `PATH` included.

| Option | Effect |
|--------|--------|
| `-h`, `--help` | Print the usage summary and exit. |
| `-V`, `--version` | Print the version and exit. |
| `-v`, `--verbose` | Verbose mode (extra detail in the trace log). |
| `-l NUM`, `--limit=NUM` | Stop with an error after `NUM` instructions (a runaway-loop guard; default 100000000000). |
| `--trace=FILE` | Write the execution trace to `FILE` instead of standard output. |
| `-d`, `--debug` | Turn on tracing: every instruction as it executes, plus register and memory changes. |

When `b6sim` finishes, its own exit status is the status the program passed to `_exit()`
(system call 1). So `echo $?` after a run reports the guest program's exit code.

## 6. A first program, end to end

Here is a complete BESM-6 assembly program that prints `hello` and exits. It is written in
the `b6as` assembly language ([Assembler_Manual.md](Assembler_Manual.md)); the only unusual
parts are the two system calls.

```asm
    .data
message:
    .ascii "hello\n"
ptr:
    .word 06400000000000000 + message   // char* fat pointer to message

   .text
_start:
    xta #1          // ACC = 1 (stdout)
    xts ptr         // push the fd, then ACC = char* to the text
    xts #6          // push the pointer, then ACC = byte count (the last argument)
    $77 4           // write(fd, buf, len)  — syscall 4

    xta
    $77 1           // _exit(0)             — syscall 1
```

A few notes for a first reading:

- **`$77 N`** is how a program asks for system call `N`: `4` is `write`, `1` is `_exit`.
- **`xta #1`** loads the accumulator with the immediate value 1 (the file descriptor for
  standard output). **`xts`** is the "push and load" instruction: it pushes the current
  accumulator onto the stack and loads a new value — the natural way to lay down arguments so
  that the last one is left in the accumulator, exactly as the calling convention wants.
- **`ptr`** holds the `char *` fat pointer to the string. The constant `06400000000000000`
  (octal) sets the byte-offset bits for "byte 0 of the word"; adding `message` fills in the
  word address. (When there is a `libc`, you will never write this by hand — the compiler
  does it for you.)

Assemble it, link it, and run it:

```console
$ b6as -o hello.o hello.s
$ b6ld -o hello.out hello.o
$ b6sim hello.out
hello
$ echo $?
0
```

That is the whole loop. A minimal "just exit with a status" program is even shorter — load
the status into the accumulator and issue `$77 1` — and `b6sim`'s exit code will be that
status, which makes exit codes easy to check from a shell script or a test harness.

> **Today's workflow is assembly → `b6as` → `b6ld` → `b6sim`.** The C front end (`b6cc -E`
> and `-S`) works, but compiling all the way to a runnable binary is still being wired up
> (the assembler and code generator are being reconciled, and there is no `crt0`/`libc` to
> link against yet). As those land, C programs will plug into the very same final `b6sim`
> step shown above.

## 7. System calls

`b6sim` implements the Unix v7 system-call set. Each call takes its arguments and returns its
result through the accumulator/stack/`r14` convention described in [§3](#3-how-it-works);
because every scalar is one word, structures such as `struct stat` are simply one word per
field. The six calls with a second result deliver it in `r12` — see [§3](#3-how-it-works).

| Group | Calls |
|-------|-------|
| Process | `exit`, `fork`, `exec`/`exece`★, `wait`, `getpid`, `getuid`/`setuid`, `getgid`/`setgid`, `nice`, `kill`, `signal`†, `pause`, `alarm`, `times`, `break`¶ (heap/`sbrk`) |
| Files & I/O | `open`, `creat`, `close`, `read`, `write`, `seek` (`lseek`), `dup`, `pipe`, `stat`/`fstat`, `access`, `stty`/`gtty`‡ |
| Filesystem | `link`, `unlink`, `chdir`, `chroot`, `chmod`, `chown`, `mknod`, `utime`, `umask`, `sync` |
| Time | `time`, `ftime`, `stime`§ |
| Accepted no-ops | `ioctl`, `lock` |
| Rejected (`EPERM`) | `mount`, `umount`, `ptrace`, `profil`, `acct`, `phys` — not meaningful for a user-level simulator |

★ `exec`/`exece` reload the image and lay the argument block at `070000` described in
[§3](#how-a-program-starts) — the same code that starts the very first program, so a guest sees
one ABI whether it was started from the command line or by another guest. An argument list past
`NCARGS` (5120 bytes, as in the kernel) is refused.
† `signal` supports only `SIG_DFL` and `SIG_IGN`; a custom guest handler cannot run from the
host's signal context and returns `EINVAL`.
‡ `stty`/`gtty` honour only "is this a terminal?".
§ `stime` cannot set the host clock and quietly succeeds.
¶ `break` takes a virtual **word** address, not a byte count — the fat-pointer marker and byte
offset are masked off, so a `char *` and a plain word address are both accepted. The new break is
rounded up to a whole page, and the call fails with `ENOMEM` if that reaches `070000` — the stack
base, and therefore the argument block, not the current `r15` (which starts above the block and
climbs). The kernel's own gate follows the same rule (`sbreak()` in `kernel/sys1.c`, whose
ceiling is `estabur()`'s `nt + nd > USTKPAGE * PGSZ`), so one `sbrk()` serves both.

Files, standard input/output/error, and pipes map straight onto the corresponding host
descriptors, so a program's output appears in your terminal and the files it creates are real
files on your disk. An unrecognised system-call number stops the simulation with an error.

## 8. Tracing and debugging

Because `b6sim` is an interpreter, it can narrate exactly what the program does. This is the
main way you debug a misbehaving compiler output or runtime routine.

`-d` (or `--debug`) turns the narration on. There is nothing to select: the trace shows every
instruction as it executes, together with the registers and memory words it changed.

```console
$ b6sim -d hello.out
--- Reset
      M17 = 70170
00012 L: 00 010 0010 xta 10
      Memory Read [00010] = 0000 0000 0000 0001
      ACC = 0000 0000 0000 0001
      RAU = 04
00012 R: 00 003 0017 xts 17
      Memory Write [70170] = 0000 0000 0000 0001
      Memory Read [00017] = 6400 0000 0000 0015
      ACC = 6400 0000 0000 0015
      M17 = 70171
...
00013 R: 00 077 0004 *77 4
      Memory Read [70170] = 0000 0000 0000 0001
      Memory Read [70171] = 6400 0000 0000 0015
hello
      M17 = 70170
00014 L: 00 010 0000 xta
      ACC = 0000 0000 0000 0000
00014 R: 00 077 0001 *77 1
```

An instruction line shows the word address (octal), whether the left (`L`) or right (`R`)
instruction of the word is running, the raw encoding, and the disassembled mnemonic; the
indented lines under it are the memory accesses it made and the registers it changed (only
those that actually changed). A system call — `*77 4` above — prints its arguments as the
memory reads that fetch them, and the program's own output (`hello`) appears inline. Send a
long trace to a file with `--trace=trace.log`, and cap a program that might loop forever with,
say, `--limit=100000`.

The `M17` at reset is `70170` here rather than `70000` because the stack starts above the
argument block ([§3](#how-a-program-starts)); the exact value depends on how long the program
name and the inherited environment variables are, so your own trace will differ in that digit
and in the stack addresses derived from it.

A program can also switch tracing on and off itself: `vtm N,0` — a `VTM` naming register 0 —
enables the trace for a non-zero `N` and disables it for `N = 0`, which is the way to narrow a
long run down to the region of interest.

## 9. Exit status and errors

- A clean exit happens when the program calls `_exit(status)` (`$77 1`); `b6sim` returns that
  `status` as its own exit code.
- A **failed system call** sets the accumulator to −1 and puts the error number in `r14`,
  just as on real Unix — the program is expected to notice and react. `r12` is left alone on
  the error path, so a two-value call's second result is meaningful only when `r14` is 0. The
  simulation keeps running.
- A **fatal machine error** stops the simulation and prints a diagnostic to standard error.
  These include an illegal instruction, an unimplemented system call, running past the
  `--limit`, or a file that is not a BESM-6 `a.out`:

  ```console
  $ b6sim not-a-program
  Error: Not a BESM-6 a.out binary: not-a-program
  ```

  On a machine fault the message includes the octal program counter, e.g.
  `Error: Jump to zero @00042`, so you can find the offending instruction in a disassembly.

## 10. Using it to develop and verify

The intended development loop is short:

1. Write or compile a small program (assembly today; C as the compiler matures).
2. Assemble and link it with `b6as` and `b6ld`.
3. Run it under `b6sim` and compare the output — and the exit status — against what you
   expect (often, against the same program built and run natively).
4. When something is wrong, re-run with `-d` and read the trace to see exactly where the
   machine diverges.

This is how the compiler back-end and the runtime routines are checked: a routine such as
`putch` or `exit` is nothing more than a small assembly stub around a `$77 N` extracode (see
[`cmd/sim/tmp/putch.s`](../cmd/sim/tmp/putch.s) and [`cmd/sim/tmp/exit.s`](../cmd/sim/tmp/exit.s)),
and `b6sim` lets you confirm it does the right thing.

The simulator's own correctness is pinned down by a GoogleTest suite,
[`cmd/sim/test/sim_test.cpp`](../cmd/sim/test/sim_test.cpp), which drives the syscall
convention directly (`write`, `stat`, `pipe`, `fork`/`wait`, a file round-trip, and more). Run
it, along with the rest of the toolchain's tests, with `make run`.

## 11. Limitations

`b6sim` is a user-level model, not the real machine, and deliberately leaves out anything that
needs a full operating system:

- No custom signal **handlers** (only default/ignore), no `ptrace`, no privileged or device
  operations, no `mount`.
- No separate filesystem — it uses the host's files, users, and permissions directly.
- One flat program image and one thread of control; `fork` uses the host's `fork`.
- It does not model instruction timing or the real machine's peripherals — for that, use the
  full-machine [SIMH](https://github.com/besm6/simh/tree/master/BESM6/) emulator.

## 12. See also

- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — argument passing and the
  `r12`/`r14`/accumulator return convention used by every system call.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — words, `sizeof(int) == 6`,
  and the `char *` fat-pointer layout.
- [Assembler_Manual.md](Assembler_Manual.md) and [Linker_Manual.md](Linker_Manual.md) — the
  `b6as`/`b6ld` tools and the `a.out` format that `b6sim` loads
  ([`cross/besm6/b.out.h`](../cross/besm6/b.out.h)).
- [besm6/c-compiler](https://github.com/besm6/c-compiler/) — the external C cross-compiler.
- [besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) — the full-machine BESM-6
  emulator.
