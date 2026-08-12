# `stty`, and eleven capabilities that describe nothing

Task C6. The C11 pass is [stty.c](stty.c)'s own header and is not repeated here. What follows is
the one decision that is not mechanical — and it is the same decision
[getty/README.md](../getty/README.md) recorded one task earlier, taken against the same driver and
reaching the same answer from the other end.

## The port is the cut

v7's `stty` is two tables and a loop. `speeds[]` has seventeen entries and `modes[]` fifty-eight,
and between them they name a dial-up terminal: baud rates, parity, five kinds of delay, and six
whole terminals that are bundles of those delays. **None of it describes a Consul-254**, and the
question a port has to answer is not "does it compile" — it all compiles — but "what happens when
somebody types it".

The answer for an entry the kernel does not honour is *nothing*, silently, with a success status.
`stty 9600` would set `sg_ospeed` and the terminal would go on at whatever rate a parallel
typewriter has. That is worse than an error, because it reads as a machine that has baud rates.

So eleven groups came out. **Every one was measured against
[kernel/dev/tty.c](../../kernel/dev/tty.c)'s `ttioccomm()` and `ttyoutput()`** rather than argued
from the hardware, because the hardware argument is what `getty` had already made and the question
here is narrower: not "is there a baud rate" but "does any line of this kernel read `sg_ospeed`".

| out | what the kernel does with it |
|---|---|
| `speeds[]`, `prspeed()`, `speed[]`, `gspeed` | `ttioccomm()` stores `sg_ispeed`/`sg_ospeed` and hands them back. Nothing between reads them; `scopen()` leaves them at `B0` because `struct tty sc[NSC]` is bss. |
| `even` `-even` `odd` `-odd` | `grep ODDP kernel/` finds the header and nothing else. The line is eight bits of data. |
| `cr0`–`cr3`, `nl0`–`nl3`, `tab0`–`tab2`, `ff0` `ff1`, `bs0` `bs1`, and `delay()` | `ttyoutput()`'s tail is column bookkeeping. It generates no delays at all: on an eight-bit line a queued byte above `0177` is *data*. |
| `33` `tty33` `37` `tty37` `05` `vt05` `tn` `tn300` `ti` `ti700` `tek` | each is a bundle of the row above. |
| `lcase` `LCASE` `-lcase` `-LCASE` | **the kernel does implement this one, in both directions.** |
| `hup` | `TIOCHPCL` sets `HUPCLS`; `grep HUPCLS kernel/` finds the store and no load. `scclose()` calls `ttyclose()` and looks at nothing. |

`XTABS` is the one bit of the delay mask that stayed, and it is why the mask could not simply be
deleted: `TBDELAY` *is* `XTABS`, tab expansion is live, and `tabs`/`-tabs` are the words for it.

## `LCASE` is the interesting one, and it comes out anyway

Everything else in that table was cut because the kernel ignores it. `LCASE` is not: `ttyinput()`
folds `A`–`Z` and `ttyrend()`'s `maptab[]` handles the backslash escapes, and `ttyoutput()`
generates them on the way back. A program that set it would get exactly what v7 promised.

It comes out because of what it would do to *this* machine. `getty`'s README states the mechanism:
the Consul's own code (GOST-10859) has no lower-case Latin letters at all, which is why
[kernel/dev/sc.c](../../kernel/dev/sc.c) runs the SIMH line `raw` and speaks ASCII — and since task
C11 this system's text is UTF-8 end to end. `LCASE` folds `a`–`z` to `A`–`Z` on output and the
reverse on input; on a UTF-8 line it also does nothing at all to the Cyrillic half, since those
bytes are above `0177`. So the capability is not inert, it is **actively wrong**: an `stty lcase`
would offer to break the console in a way that looks like a feature.

**A capability the kernel honours is not automatically one to expose.** That is the cut's one
transferable result, and it is the opposite of the rule the other ten rows follow.

## `TANDEM` is the mirror image, and is left alone

`TANDEM` is live — `ttyblock()` queues the stop character when the input queue passes `TTYHOG/2`,
and `canon()` sends the start character back — and **no program on this image can set it**, because
v7's `stty` had no word for it and this port added none.

That is deliberate and it is the conservative half of the same judgement. The cut is *subtractive*,
on `getty`'s precedent: a port decides what of v7 survives, not what v7 should have had. And on a
typewriter with no keyboard buffer to stop, flow control means about as much as the delays do.
[../README.md](../README.md) carries it as a named loose end rather than this program inventing a name
for it — the same treatment `TIOCEXCL` gets, which `ttioccomm()` accepts and nothing tests.

## Three bugs the cut did not cover

**`stty erase` with the keyword last walks off the end of `argv`.** v7:

```c
if (eq("erase")) {
    if (**++argv == '^')  ...
    argc--;
}
```

The loop condition is `--argc > 0`, so on the last argument `++argv` is `argv[argc]` — the null
terminator — and `**argv` dereferences it. `stty kill` is the same line again. Both are bounded
against `argc` here and give a diagnostic.

**`gtty()`'s return was ignored.** On anything that is not a terminal, `mode` stays the zeroed bss
it started as and v7 cheerfully reports `speed 0 baud`, `erase = '^@'`, no flags. It is a plausible
answer to a question that was never asked, which is [../README.md](../README.md) §11's shape of
failure in a different medium: *junk that looks like output*. It is an error now — which is also
what makes the program honest under a harness that has no terminal at all.

**`prmodes()` could not print DEL, and `erase ^?` could not set it.** v7's test is `c < ' '`, and
0177 is above it, so a DEL erase character went into the report as a raw DEL byte; the argument
side masked `^X` with `037`, which cannot reach 0177 either. Both were harmless while the defaults
were `#` and `@`, and neither is now that `CERASE` is `^?` ([`include/sys/tty.h`](../../include/sys/tty.h)).
`prchar()` spells it `^?` on the way out, and `^?` on the command line means DEL on the way in —
4BSD's rule in both directions.

## Where it is tested

[../../kernel/test/multi](../../kernel/test/multi), stages 25–29, and nowhere else. There is no
`b6sim` half: §9's rule is that a program whose subject is a terminal has none, and under that
harness `ioctl` is an unconditional success that changes nothing — every case would be asserting
the simulator.

What the booted stages pin is that the report is **exactly as long as the truth**: `erase = '^?';
kill = '^U'` over `-nl echo -tabs`, which is `scopen()`'s own first-open state and nothing else.
Then `stty tabs`, the report again with `-tabs` gone, and `stty -tabs` to put it back — a round
trip through `stty(2)` and `gtty(2)` and the same `struct sgttyb`. The flag order is v7's
`prmodes()` order and not the table's: `CRMOD` prints before `ECHO`.

## Sizes

|  | const | text | data | bss | total |
|---|---|---|---|---|---|
| `stty` | 79 | 2,774 | 241 | 1,035 | **4,129** |

Of the 28,672 available. The tables that came out were the program's only bulk; what is left is
stdio and a fifty-line loop.
