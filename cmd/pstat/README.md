# `pstat` grew three modes because the kernel was already answering

Task C8's fifth ([../TODO.md](../TODO.md)), and the widest reader of the kernel-variable table:
eleven of `kernel/ksym.c`'s thirty-three rows name this program. Two things about the port are
structural and neither is in the brief. [pstat.c](pstat.c)'s header is the short form;
[pstat.1m](pstat.1m) carries the user-facing divergences, each marked `Note:`.

## 1. It is not root-only, and the brief said it was

[../TODO.md](../TODO.md) puts `ps` and `pstat` together as the pair that "run as root and only
root, exactly as v7's did". That is right about `ps`, whose u-area has to come off a memory
device, and it is **wrong about this one**. Every table `pstat` prints — `inode`, `file`,
`text`, `proc`, `mount`, `sc`, `coremap`, `swapmap`, `nswap`, `swplo`, `swapdev` — comes
through `kctl(2)`, and [`<sys/kctl.h>`](../../include/sys/kctl.h) is explicit that the call is
**not privileged**: what is guarded is the memory devices, not the variables' values. So seven
of the eight modes work for anybody, and only `-u` — which opens `/dev/kmem` or `/dev/mem`,
both mode 0640 and root's — does not.

That is a real change in the privilege model against v7, where the whole program was gated
behind `/dev/mem`, and it is a change in the direction that matters: **the numbers a system
inspection program prints are not secrets; the memory it would have to read to get them was.**
`kctl(2)` separates the two, which is the argument for its existing at all.

It stays **not setuid**, and must. `/dev/mem` is every process's memory, and a setuid `pstat`
would hand it out through a program that already knows the layout —
[../quot/CMakeLists.txt](../quot/CMakeLists.txt) makes the same argument about `/dev/rmd0`.

## 2. The table's discipline forced three features

[`kernel/ksym.c`](../../kernel/ksym.c) states its own rule: *a row names the program that asked
for it, and a row whose column is empty does not belong.* The rows were put there
before any of these programs existed, on the strength of what they were expected to want.
Checking that when they arrived found eight rows in trouble:

| rows | claimed by | actually read by |
|---|---|---|
| `text` | `ps`, `pstat` | `pstat` only — `ps -l` has no TEXTP column and v7's had none either |
| `lbolt`, `time` | `ps` | **nobody** — `ps` has neither a START nor an ELAPSED column |
| `mount`, `coremap`, `swapmap`, `nswap`, `swplo`, `swapdev` | `pstat` | **nobody** — v7's `pstat` prints not one of them |

Deleting eight rows was one settlement and it was the wrong one: they are live, correct
variables that a system-inspection program plainly ought to be able to show. So `pstat` grew:

* **`-m`**, the mount table. One line per mounted filesystem, `LOC`/`DEVICE`/`BUFP`/`INODP`.
* **`-s`**, the paging store and the two allocation maps. **Their units differ** —
  [`<sys/map.h>`](../../include/sys/map.h) says `coremap` hands out *words* of physical core
  and `swapmap` *blocks* of `BSIZE` — so they are printed under separate headings with the
  units named, one table of both being exactly the silent unit change §4 of
  [../README.md](../README.md) is about.
* **`-c`**, the system clock. `time` is seconds since the epoch and `lbolt` is ticks into the
  current second ([../../kernel/clock.c](../../kernel/clock.c)); `time(2)` returns whole
  seconds, so **the fraction is visible nowhere else in userland**. This is the one of the
  three that buys something new rather than merely exposing a table.

The lesson generalises past this program: **a discipline that makes "who reads this" a
question with a required answer will occasionally answer it with a feature nobody had written
yet, and that is the discipline working rather than failing.**

## 3. `dotty()` had to be written from scratch

v7's reads three symbols — `_kl11`, `_dh11[48]`, `_ndh11` — and prints `1 kl11` before it does
anything else. All three are PDP-11 communications hardware; there is no Unix in the routine
at all. Here there is one terminal driver and one array, `sc[NSC]`, the two Consul typewriters.

Two columns changed with it, and one of them is a correction rather than a translation:

* **`ADDR` became `LINE`.** [`<sys/tty.h>`](../../include/sys/tty.h) says outright that
  `t_addr` is a device/line number and **not an address** — this machine has no I/O address
  space, a device being named by the register number an `033` instruction addresses. v7's
  column was the UNIBUS address of the line's registers. Keeping the heading would have been a
  lie in a program whose entire job is to name things accurately.
* **`STATE`'s eight letters survive**, `T W O C B A X H`, and two of them record a request
  rather than a state: nothing in this kernel sets `TIMEOUT`, and `XCLUDE` and `HUPCLS` are
  set by `ioctl(2)` and tested by nothing. `<sys/tty.h>`'s own comment says `TIMEOUT` is
  "kept for v7's numbering and for `pstat(8)`" — the header was expecting this program.

## 4. Two things about the compiler, and the first is unique in the tree

**`<sys/mount.h>` and `<unistd.h>` cannot both be included.** The first declares the kernel's
table, `extern struct mount mount[NMOUNT]`; the second declares the system call,
`int mount(const char *, const char *, int)`. Same name, and `b6parse` refuses the second one
it sees:

```
Fatal error: Variable mount redeclared with different type
```

Nothing had hit it before because nothing had ever wanted the mount *table* from user space —
`mount(1M)` wants the call, and the kernel wants the table and links no libc. [pstat.c](pstat.c)
is the one source in this tree that cannot include `<unistd.h>`; it declares `read`, `close`
and `lseek` by hand, copied from that header and required to stay identical to it. The other
way out — dropping the `extern` from `<sys/mount.h>` — is not available: kernel sources rely
on it, and [../../include/README.md](../../include/README.md)'s rule is that every header
stands alone.

**A local named `free` is an error, not a shadow.** This compiler refuses the redeclaration
against `<stdlib.h>` outright rather than shadowing it. `prmap()`'s counter is `avail`.

## 5. Every table is bss

v7 declares each one as an automatic inside the routine that prints it, and
`struct proc xproc[NPROC]` alone is **1,800 words of the 4,096** the user stack has. That is
§6's third ceiling and nothing checks it. All eight tables here are file-scope statics; they
come to about 2,980 words together, a tenth of the image ceiling and none of the stack.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `pstat` | 149 | 4,604 | 486 | 4,027 | **9,266** |

## What each harness can say

[test/](test/) has seven checked-in literals and one oracle. The literals are the **empty**
tables, and they are literals precisely because b6sim's imitation kernel leaves `inode`,
`file`, `text`, `mount`, `sc` and both maps zero on purpose — "a plausible fiction is worse
than an empty one" ([../sim/kernel.h](../sim/kernel.h)). Two of them do more than record a
heading: `tty` pins `NSC` and the 29-word `struct tty` stride in two rows, and `swap` pins
three scalars, b6sim storing `conf.c`'s own `swapdev`, `swplo` and `nswap` rather than
inventing any.

[test/run-pstat-test.sh](test/run-pstat-test.sh)'s subject is the **cross-reference**: `-p`
computes a `LOC` from `kgetsym("proc")` and the struct's stride, `-u 0` reads `u_procp` out of
the live u-area, and the two must be the same number. That is
[../../lib/test/kctlt.c](../../lib/test/kctlt.c)'s check made from outside the kernel's
headers, and a table pointing at the wrong array — or at the right array with the wrong
stride — passes everything else here and fails it.

Every number this program exists to print needs the boot, and
[../../kernel/test/inspect](../../kernel/test/inspect) is where it gets one.
