# `getty`, and a table with nothing left in it

Kernel task 29b, and the first of the two programs that take this machine out of single-user.
The C11 pass is [getty.c](getty.c)'s own header and is not repeated here; what follows is the
one decision that is not mechanical, and the one thing it revealed about the terminal path.

## The speed table is gone, and that is the whole port

v7's `getty` is a table-driven program. `itab[]` has thirteen entries — `B110` through `B9600`
— and each carries an initial mode word for reading the name, a final one for handing to
`login`, a greeting, and a **successor**: press `break` at the wrong speed and `getty` moves to
the next entry and prints the greeting again. That is what `getty` *is*: a program for guessing
the speed of a dial-up line.

None of it survives, because none of it describes a Consul-254. The Consul is a **parallel,
character-at-a-time typewriter**: one whole character goes out per `ext` instruction and each
one raises an interrupt ([kernel/dev/sc.c](../../kernel/dev/sc.c)). There is no serial line, no
baud rate to guess, no `break` key, and nothing to fall back to. Concretely, in this kernel:

* **`sg_ispeed`/`sg_ospeed` are inert storage.** `grep t_ispeed kernel/` finds only the four
  lines in `ttioccomm()` that store them and hand them back; nothing between reads them, and
  `scopen()` leaves them at `B0` because `struct tty sc[NSC]` is bss. v7's `'3'` entry probed
  the connect speed with `TIOCGETP` and chose a table from the answer — here that probe reads
  back `B0` on every line, always. It is gone with the speeds.
* **The delay bits are worse than inert.** `ttyoutput()` turns `CR1`, `NL1`, `TAB1` and `FF1`
  into a queued delay byte and `scstart()` turns that into a `timeout()`. `sc.c`'s own header
  calls that arm unreachable "as the console is opened today" — and v7's table would have made
  it reachable in the boot path of every test in the suite, to give a carriage time to return
  on a terminal that does not have one.
* **Parity is not carried.** `scstart()` sends `c & 0177` and `scintr()` masks the same way, so
  `ANYP`/`EVENP`/`ODDP` describe nothing. v7's `partab[]` — 128 bytes of even-parity flags that
  `putchr()` ORed into every character — went with them.
* **`LCASE` would undo the driver.** The Consul's own code (GOST-10859) has no lower-case Latin
  letters at all, which is why `sc.c` runs the SIMH line `raw` and speaks ASCII. A `getty` that
  turned `LCASE` on would fold that away.

So the table has **one entry**, and it keeps v7's shape rather than collapsing into two
constants: `init` still passes a selector character, [ttys(5)](../../include/man/ttys.5) still
defines the column, and a second kind of terminal is one line. An unknown selector falls back
to `itab[0]`, which is v7's rule and is now also the only outcome.

```c
static const struct tab itab[] = {
    { '0', '0', RAW, ECHO | CRMOD | XTABS, "\r\nlogin: " },
};
```

`iflags` is `RAW` so that `getname()` sees the typed CR itself and can tell a terminal that
sends CR from one that sends NL — the one piece of v7's adaptation that still means something.
`fflags` is **exactly what `scopen()` sets on a first open**, so a line handed to `login` is in
the state the rest of this system assumes; getting that wrong would have shown up as every
later test's expectations shifting under it.

## Two pointer bounds, and one of them was an overflow

`getname()` bounded its buffer with `np >= &name[16]` and `np > name` over a `char *`, which is
[cmd/README.md](../README.md) §2's hazard: a relational operator between two `char *` gives the
wrong answer here, the byte offset sitting above the word address and *decrementing* as the
pointer advances. Both are an `int` index now.

Fixing the first one exposed an upstream bug hiding behind it. v7 broke out of the loop on
`np >= &name[16]` and then wrote `*np = 0` — so a name of exactly sixteen characters stored the
terminator at `name[16]`, one past the array. The bound is `NAMESIZE - 1` here, which leaves
the room the terminator needs.

## What it cost

**434 words** of the 28,672, which makes it the smallest program on the image — smaller than
`/bin/test`'s 964. It links no stdio: `read()` and `write()` are the whole of its I/O, and
`_exit()` rather than `exit()` keeps `<stdlib.h>` out too. v7's local `puts()` is `putmsg()`
here, because the C11 name is taken.

## What it is tested by, and what it is not

`kernel/test/login` — a booted kernel, a real Consul, and a typed dialogue. There is no b6sim
half and there cannot be: that world's `ioctl` is an unconditional success that changes
nothing, its `gtty` zero-fills five words, `run-prog-test.sh` cannot feed stdin, and this
program does not terminate. See [cmd/README.md](../README.md) §9.

## One finding that is not this program's

Driving this dialogue needed `send delay=20000` on top of the `after=20000` every booting test
already carries, and it is the **second** character that goes missing without it, reproducibly,
on an idle machine. What is different here is that `getty` reads the name in **RAW mode** — one
`read(2)` and one `write(2)` per character, from user mode — where every other test types at a
shell in canonical mode and the kernel accumulates a whole line in a clist before waking
anybody. `scintr()` takes one character per ПРП interrupt out of a single register, so a
character arriving while the last is still unread has nowhere to wait, and a per-character
round trip through user mode is the way to make that window wide.

That is evidence for the guest-side half of [kernel/TODO.md](../../kernel/TODO.md) task 35's
question — the first case that tells the two send rates apart, since the same simulator feeds a
shell at the default rate without loss. It is not a proof; `kernel/test/login.ini`'s header says
what would be.
