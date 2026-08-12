# `crypt(1)`, `makekey(8)` and the one rotor

Task C19. Two small programs — `/bin/crypt` (93 lines of v7) and `/usr/lib/makekey` (21) — over
an implementation that was on the image before either of them: libc's `crypt(3)`, which
`login`, `passwd` and `su` have always used. What made the task worth a `README.md` is that
neither of the two decisions it forced was about either program.

## The key schedule does not survive a 41-bit `long`

v7's `setup()` turns a password into a 256-element rotor by way of an arithmetic that
**deliberately overflows**:

```c
    seed = 123;
    for (i = 0; i < 13; i++)
        seed = seed * buf[i] + i;        /* buf[i] is 46..122 */
```

Thirteen multiplications by something near a hundred put `seed` past 2⁸⁰ before the loop ends.
On a PDP-11 that is fine and it is the *point*: a `long` is 32 bits, the product wraps, and the
low bits are as good a source of rotor positions as any. `../ed/ed.c` recorded the problem when
it deleted `-x`: a `long` here is one 41-bit word, so the wrap is not v7's.

**The real hazard is worse than a different wrap, and it is why this is written down.** On this
machine an overflowing integer multiply does not wrap at all. `b$mul`
(`../../doc/Besm6_Runtime_Library.md`) is the hardware *floating* multiply with the exponent
corrected and stripped afterwards:

```asm
    a*x                         // 40-bit x 40-bit -> the HIGH 40 bits of the product
    a+x #0'64                   // correct the exponent
    aax #037'7777'7777'7777     // strip it, leaving a 41-bit signed result
```

A product that fits 41 bits comes back exactly. A product that does not comes back as its **high
bits**, the low ones having fallen off the end of the mantissa — so the loop above would not
merely derive a different rotor from a PDP-11's, it would derive one that no description of the
algorithm could predict and that a change to `b$mul`'s rounding could move.

So the arithmetic is bounded, which costs one macro:

```c
#define WRAP32(s) ((((s) & 0xffffffff) ^ 0x80000000) - 0x80000000)
```

Two things follow, and the second is the one that pays. Every operand is now under 2³¹ and every
multiplier under 128, so **every product fits 41 bits and `b$mul` is exact**. And the result is
v7's, bit for bit — so a file encrypted here can be read on a PDP-11 and the reverse, which is
what `crypt(1)` is *for*.

One more PDP-11 width had to come with it. `random` is a v7 `unsigned`, which is **sixteen
bits**, and it is assigned a 32-bit remainder:

```c
    random = (seed % 65521) & 0177777;    /* the mask is the 16-bit unsigned */
```

Without the mask the two bytes the rotor takes out of `random` — `random & 0377` and then
`random >>= 8` — are drawn from a value that never existed on the machine this algorithm was
written for. It is kept as an `int` rather than an `unsigned`: after the mask it is
non-negative, so the shift is an ordinary one, and §3 prefers an `int` wherever `unsigned`
bought nothing.

## One rotor, two programs

v7 wrote the machine twice — `crypt.c`'s `setup()` and `ed.c`'s `crinit()`/`crblock()` — and
`crypt.1` promises that *"files encrypted by crypt are compatible with those treated by the
editor ed in encryption mode"*. Two copies of an algorithm with a promise across them is the
shape `../df` and `../umount` already refused for `/etc/mtab`, whose `CMakeLists.txt` says it
plainly: v7's copies of one layout across two programs were the bug.

So [`rotor.c`](rotor.c) is the one copy and both programs link it, `../ed/CMakeLists.txt`
naming it in its `SOURCES` exactly as `../df` names `../mount/mtab.c`. The promise stops being
a habit and becomes a property of the build.

### Why `% k` is safe

`crinit()` ends its pairing loop with `ic = (random & 0377) % k` where `k` counts down to zero,
which reads like a division by zero waiting for the last iteration. It cannot happen, and the
reason is a parity argument rather than a bound.

The loop pairs indices: at each `k` whose `t3[k]` is still 0, it finds an unpaired `ic < k` and
sets `t3[k] = ic`, `t3[ic] = k`. Each pairing removes two indices from the unpaired set, which
starts at 256, so **the number of unpaired indices is always even**. At `k == 1`: if 1 is
unpaired then exactly one other index is, and every index above 1 has been dealt with, so it is
0 — the `while` loop finds it immediately and the two are paired. If 1 is paired, then 0 is too.
Either way `t3[0] != 0` when `k` reaches 0, and the `continue` above the division is taken. v7's
code is correct as written; it is only unobvious.

## The fork is gone and `makekey` stayed

v7's `crypt(1)` builds a pipe, forks, and execs `/usr/lib/makekey` to turn ten bytes into
thirteen — because the DES code shipped separately from the rest of the system, not because
anything about the algorithm wanted a second process. Here `crypt(3)` is in libc already, so
`crinit()` calls it: **the same thirteen bytes, for one call instead of a fork, a pipe, an exec
and a wait.** `makekey` *is* that call and nothing else, so this is not an approximation.

`/usr/lib/makekey` is on the image all the same, as `makekey.8` describes it, and it is the
second program here outside `/bin` after `/usr/lib/diffh`. Nothing execs it. That is worth
saying out loud in `../../root.manifest` rather than leaving for somebody to discover.

Two corrections to its twenty-one lines. v7 ignored both `read(2)` results, so ten bytes short
of ten produced thirteen bytes derived from whatever the stack held; and it passed an
unterminated `char[8]` to `crypt(3)`, which reads one byte past it before its own `i < 64` guard
stops it.

## The filter, and §11

`crypt` must carry eight bits, which is what `../TODO.md` singled the task out for — and the
port answers it by **never looking at a byte**. v7 ran the rotor inline over
`getchar()`/`putchar()`; this reads a block, hands the block to `crblock()` and writes it back:

```c
    while ((n = fread(cbuf, 1, sizeof(cbuf), stdin)) > 0) {
        crblock(perm, cbuf, n, pos);
        fwrite(cbuf, 1, n, stdout);
        pos += n;
    }
```

There is nowhere left to put a mask. Inside `crblock()` every `& 0377` is on an *index* into a
256-entry table, which is §11's distinction exactly, and a plain `char` being unsigned here
means a Cyrillic byte indexes as itself.

The block size is not the cipher's business either. `crblock()` takes the byte's offset in the
stream and derives the rotor's position from it, so the same stream comes out the same whatever
it is run in — which is what lets `ed` use the identical routine over its 512-byte buffers, and
what the `enc` case is 3,100 bytes long to prove: it is over `BUFSIZ`, so the second `fread()`
runs with a non-zero offset.

## The oracle

The machine is an **involution** — `t2` inverts `t1`, `t3` is a pairing and so its own inverse
— which is why `crypt` has no decrypt flag and why `enc` and `dec` are one round trip written
down twice. But a round trip only says the program agrees with itself, and the whole of the
`WRAP32` argument above is a claim about agreeing with *another machine*.

So the fixtures are **a second implementation's** (§9's second oracle shape): v7's rotor over a
real `int32_t` and a real `uint16_t` on the build host, feeding the host's own `crypt(3)` —
which `../../lib/test/pwent.c` has already pinned against this libc's. Nothing of the BESM-6
port is in that path, so `cmd_crypt_enc` passing says the emulation is right and not merely
consistent. [`test/mkfix.c`](test/mkfix.c) is that reference, checked in and built by nothing;
its header is the recipe for regenerating any fixture here.

`cmd_makekey_key` has the cheaper version of the same shape: thirteen bytes from the host's
`crypt(3)`, for a key and salt chosen printable so the fixture can be read.

## What this harness cannot say

`b6sim` has no terminal, so **the no-argument form is never exercised** — `getpass(3)` falls
back to reading standard input when it cannot open `/dev/tty`, and under `ctest` that fallback
is not even reliable: run from a terminal, the fixture would hang waiting for someone to type.
That is also why `ed -x` has no case here at all (`../ed/README.md`). Both were checked by hand:

```sh
crypt hobbit </etc/passwd >/tmp/c && crypt hobbit </tmp/c | cmp - /etc/passwd
```

An empty key is the other one. `crinit()` derives the rotor from `crypt("", "\0\0")` and returns
0, and `crypt(1)` ignores the answer and enciphers with it — v7's behaviour, weak and
deliberate. `ed` is the caller that wants the return value, an empty key being its way of
turning encryption off.
