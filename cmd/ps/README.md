# `ps` needs a u-area that is not its own, and no longer reads one itself

Task C8's fourth ([../TODO.md](../TODO.md)), and the one that brief told nobody to port. This
is the account of what replaced v7's 408 lines and of the one rule the replacement turns on.

**The rule is now the kernel's.** `ps` reached the u-area through `/dev/kmem` and `/dev/mem`
until `KCTL_PSINFO`, and because both are mode 0640 and root's it was the super-user's
program. It is not any more: the kernel does the walking and hands back a four-field digest,
so `ps` opens no device and needs no privilege. Everything below still describes the rule —
it moved to [../../kernel/kctl.c](../../kernel/kctl.c), it did not go away. See
[../README.md](../README.md) §8: the fix for a program that needs privilege is to ask what it
is actually reading, not to reach for a mode or a setuid bit.
[../README.md](../README.md) §9 has the harness argument, [ps.1](ps.1) the user-facing
divergences, and [ps.c](ps.c)'s header the short form of all of it; what is here is the part
that is structural.

## The route, and why none of v7's survived

v7's `ps.c` is 408 lines of which about forty are the report. The other 368 are a *route to
the data*:

| v7 | here |
|---|---|
| `nlist("/unix", nl)` | `kctl("proc", KCTL_GET, …)` — **there is no `/unix`** |
| `n_value - 0x7fc00000 + 0x10000` | nothing; every address in the table is a link-time relocation |
| sequential `read()` of `/dev/mem` for `proc[]` | one call, by name, by value |
| `chdir("/dev")` + `fopen("/dev")` + `stat` per entry | `kgetsym("sc")` and one subtraction |
| u-area at `ctob(p_addr)`, or `(p_addr+swplo)<<9` on `/dev/swap` | `UBASE`, or `p_addr` on `/dev/mem` |
| `getptr`/`getbyte`/`within`/`round`/`datmap` — 140 lines walking the process's stack through a two-segment map to rebuild `argv` | `u_comm`, one `printf` |

The last row is the largest and it is not a cleverness: `u_comm` is in the u-area on this
system (`<sys/user.h>`), `exec()` fills it, and v7's own manual page warned that its
reconstruction was "inherently somewhat unreliable" and that "a process is entitled to destroy
this information". The price is that there are **no arguments**, only the name, and it is at
most `DIRSIZ` characters.

**The general rule, and it is why this is written down rather than left in the source: when a
v7 program's bulk is its route to the data rather than its treatment of it, the port is a
rewrite and comes out a third of the size.** `pstat` next door is the same story with
`dotty()`; C9's `as` and `ld` will be it again.

## The three-place rule

A u-area is in one of exactly three places and choosing wrong is silent — the numbers still
print, they are just one context switch old or somebody else's.

1. **`p_addr == uhome`** — the live copy at `UBASE` is authoritative and the copy in the
   process's own image is stale, `uflush()` not having run for it yet
   ([../../kernel/text.c](../../kernel/text.c)). The kernel runs unmapped, so it reads `u`
   directly; `ps` used to read `UBASE` through `/dev/kmem`.
2. **`SLOAD` set and `p_addr != uhome`** — the image is in core and its first `USIZE` words
   are the saved u-area, current as of that process's last context switch. `p_addr` is at or
   above `KREACH`, so this goes through `copyphys()`; `ps` used to go through `/dev/mem`.
3. **`SLOAD` clear** — swapped out, and `p_addr` is a block on the paging store rather than an
   address. v7 read it back off `/dev/swap`; this does not, and the row prints `<swapped>`.

`uhome` ([../../kernel/switch.s](../../kernel/switch.s)) is a plain kernel variable to the
code that now reads it, and stopped being a `kctl` table row when `ps` stopped needing one.
`NOUHOME` is 0 and no image is ever at word 0, so case 1 is safe when the live u-area belongs
to nobody. [../../lib/test/memt.c](../../lib/test/memt.c) is the rung below this
program: it climbs the same ladder from `UBASE` through `u_procp` into `proc[]` and out to
physical memory above `0100000`, and it was written before this task, for this task.

## A v7 bug that this kernel makes real

v7's `prcom()` reads the u-area first and tests `SZOMB` twenty lines later. On this kernel
`exit()` ([../../kernel/sys1.c](../../kernel/sys1.c)) calls `mfree(coremap, p_size, p_addr)`
and *then* sets `p_stat = SZOMB` — so a zombie's `p_addr` names core that has already been
handed back and may hold another process's image by the time `ps` looks. The test moved ahead
of the read: a zombie's u-area is not read at all.

## The tty column is an index, not a directory scan

v7 read `/dev` as a raw `struct direct` stream, `stat`ed every entry, matched `st_rdev`
against `u_ttyd` and stripped a leading `tty`. Here `sc[NSC]` is the only terminal array in
the kernel and `sc[minor(dev)]` is how the driver itself indexes it
([../../kernel/dev/sc.c](../../kernel/dev/sc.c)), so

```c
i = (ptrword(u.u_ttyp) - ptrword(sc)) / (sizeof(struct tty) / NBPW);
```

*is* the terminal's name, and it prints the same digit v7's scan produced. It is computed in
the kernel now and arrives as `ps_ttyn`. Two reasons to prefer it beyond the eighty lines it
saves. **The stride is computed, never written** — 29 is also a hand-measured constant in
[../sim/kernel.h](../sim/kernel.h), and the whole point of deriving it from the real header is
that a divergence between the two shows up as a failing test rather than as a wrong column. And **`u_ttyp` and not `u_ttyd`**: b6sim stores
only the pointer and leaves the device number 0, so the `u_ttyd` route would print `?` under
the simulator and `0` on the image — a divergence that would then have to be excused.

## What each harness can say

[test/](test/) has two literal cases (an unknown option, a pid that matches nothing — the two
paths that print no process and so no volatile number) and one oracle,
[test/run-ps-test.sh](test/run-ps-test.sh), which checks the columns against things the
*shell* independently knows. That is more than a shape check: `ADDR` comes back `0100000` and
`SZ` `32768`, which are b6sim's `KREACH` and its whole memory, so the case pins the proc
table's **base and stride** as well as the format — read them off the wrong array or at the
wrong offset and you land in a zero slot and print nothing at all.

What it cannot reach is the second and third cases of the three-place rule.
[../../kernel/test/inspect.sh](../../kernel/test/inspect.sh) does: b6sim's one process has
`p_addr == uhome`, so a `ps` there *always* takes branch 1, and only on the image is there an
`init` to look at. Section 7 of that script takes `init`'s ADDR out of a `ps -l` listing and
hands it to `pstat -u`, which is branch 2 — a u-area above `KREACH`, off `/dev/mem`, belonging
to somebody else. That is the sharpest single assertion in task C8. Since `KCTL_PSINFO`,
`pstat -u` is the only half of it that still needs `/dev/mem`, and `ps` reaches branch 2
through the kernel instead — which is what
[../../lib/test/unprivt.c](../../lib/test/unprivt.c) exists to prove, by running both
programs as `guest` after checking that the three device nodes still refuse that uid.

One thing found while writing it and worth keeping: `sleep 30 &` returns the moment the shell
has **forked**, and a `ps` run immediately after finds a process still called `sh`, in state R,
with `SLOAD|SSWAP` set and no `WCHAN` — a correct picture of a process caught mid-`exec`. Two
seconds later the same slot is `sleep`, asleep on a channel. The test waits, and says why.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `ps` | 86 | 2,989 | 212 | 3,738 | **7,025** |

Nearly all the bss is two tables: `struct proc ptab[NPROC]` at 1,800 words and
`struct psinfo psi[NPROC]` at 900. **Both are file-scope statics and must stay so** — together
that is 66% of the 4,096-word stack, which is §6's third ceiling and the one nothing checks.
[../../lib/test/kctlt.c](../../lib/test/kctlt.c) says the same thing in its own comment and is
the precedent. Text fell by 89 words even so: `getu()`, `rdwords()` and the two `open`s went.
