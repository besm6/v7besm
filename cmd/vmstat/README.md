# `vmstat` — the rate report, and the kernel instrumentation it forced

Not a v7 program. v7 has no `vmstat`; the source is
[RetroBSD's](../../../BSD/retrobsd/src/cmd/vmstat.c), by way of 2.11BSD. It is here because
[../../kernel/ksym.c](../../kernel/ksym.c) had been holding a dozen live counters open on one
sentence — *"they come with a vmstat"* — and because the disk half of
[../iostat/](../iostat/) had been waiting on a kernel task with the same shape. Both are
discharged here.

## What the source asked for, and what answered

RetroBSD's `vmstat` reads thirteen `nlist` entries: `cp_time`, `rate`, `total`, `forkstat`,
`sum`, `boottime`, `dk_xfer`, `hz`, `nchstats`, `dk_ndrive`, `dk_name`, `dk_unit`, `freemem`.
Of the structures behind them — `vmrate`, `vmsum`, `vmtotal`, `forkstat`, `nchstats` — **this
kernel has none**, and neither has it the `vmmeter()`/`vmtotal()` pair that fills them once a
second and once every five. What replaced each:

| BSD wanted | here |
|---|---|
| `cp_time[4]` | `dk_time[32]`, which already existed. Four CPU states crossed with eight I/O states; a category is the **sum of eight slots** |
| `total` (`t_rq`, `t_dw`, `t_sw`, `t_avm`) | a scan of `proc[]` **in user space** |
| `freemem` | the sum of `coremap`'s extents |
| `rate`/`sum` | ten scalars, six of which were already live and four of which are new |
| `dk_xfer[]` | `dk_numb[]`, kept by the two drivers for the first time |
| `dk_ndrive`, `dk_name[]`, `dk_unit[]` | `NDK`, `DK_MD`, `DK_MB` in `<sys/param.h>` |
| `hz` | `HZ` in `<sys/param.h>` |
| `boottime` | nothing — see below |
| `forkstat`, `nchstats` | nothing. There is no fork accounting and no name cache |

**The proc scan is the substantive one.** BSD recomputes `total` inside the kernel every five
seconds and a `vmstat` reads the answer, which is kernel code, kernel data and an answer up to
five seconds old. Both tables here are exported already for `ps` and `pstat`, the scan is 150
rows in user space, and it costs the kernel nothing and is never stale.

## Why there is no `boottime`

`clock()` reaches its `dk_time[a] += 1` on **every** path — the callout early-out is a `goto
out` and `out:` sits above the increment, and the one `return` is after it. So

    Σ dk_time[0..31] == timer interrupts serviced since boot == uptime × HZ

and the interval is that over `HZ`. Three things make it the right denominator rather than
merely an available one:

1. It is the **same** denominator the percentages use, so no two numbers in one report can
   disagree about how long the interval was. A wall clock would let them.
2. It needs no new kernel variable at all — which is exactly the simplification `ksym.c`'s
   doctrine already made against RetroBSD's `_hz` and `_nproc`.
3. The first report and every later one then share **one** formula, where BSD needed a
   `nintv != 1` branch and a second kernel structure to read on the first pass.

What it measures is ticks *accounted*. `GRP_TIMER` is a flip-flop and the timer free-runs, so
a tick arriving while delivery is blocked is coalesced; this is a lower bound on wall clock,
and `iostat` has always printed under the same convention.

## Why `dk_busy` is written now and still not exported

The drivers keep it — [../../kernel/dev/md.c](../../kernel/dev/md.c) sets bit `DK_MD` while an
exchange is outstanding and [mb.c](../../kernel/dev/mb.c) sets bit `DK_MB` — so the old reason
for withholding it (a variable nobody writes makes a tool print zeros that read as
measurements) is gone. It stays out of the table for a better one: **it is an instant, not a
count.** Read between two ticks it says only what the drives happened to be doing during the
read, and no interval divides it. What a report wants is `dk_time`, whose low three bits *are*
`dk_busy` sampled at every tick — so a per-device busy percentage falls out of the histogram
and needs nothing else. `dk_numb` and `dk_wds` *are* exported: those are counts.

**One slot per controller, not per drive.** `dk_busy` is three bits wide because it is the low
three of `clock.c`'s subscript, and `md.c` addresses `MDNUNIT` = 64 units. A per-drive
breakdown needs a wider histogram than `dk_time` is, and nothing has asked for one.

## Where the counters are bumped

| counter | site | and not |
|---|---|---|
| `dk_numb`, `dk_wds` | `mdintr()`/`mbintr()`, where a chunk **landed** | at issue — a soft error re-issues the same chunk `MDRETRY` times and lands none of them |
| `dk_busy` | tracks `b_active` exactly | anything finer; a chained or retried request really is busy throughout |
| `nintr` | `extintr()`, per **source serviced** | per entry — one vector can carry a completion and a tick |
| `nsyscall` | `syscall()` | `badextr()` — э50–э76 raise SIGILL and are not system calls |
| `ntrap` | `trap()`, before the decode | after it; `grow()`'s stack retry is the commonest fault and is counted |
| `nswtch` | `swtch()`, after a process has been chosen | at the head — the loop re-enters through `idle()` |

## Testing

Two halves, and the split is the usual one. [test/](test/) runs it under `b6sim`, where the
assertion is **shape**: the headers are exact, every column is where its heading says and holds
an integer, the CPU percentages partition 100 to within the independent rounding — and the
columns b6sim structurally cannot move (`b`, `w`, `md`, `mb`, `in`, `tr`, `cs`) are **zero**,
which is the honesty check rather than a gap. `kernel/test/inspect` is where they move, on the
booted image, and the driver counters themselves are held to a known answer in
`kernel/test/mdtest` and `kernel/test/mbtest`.
