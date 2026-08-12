# `units`, and the fault that is not a signal

Task C18. `units(1)` is the first program on this image whose whole subject is arithmetic on
real numbers, and it arrived with a mechanism that could not work here: v7 catches its own
overflows with `signal(8, fperr)` and reports them from a flag the handler sets. Nothing in
this kernel had ever raised `SIGFPE`. Worse, an overflow was not a signal that went missing —
it was a **machine fault** that reached `trap()`'s `panic("trap")` and stopped the machine.

So the port is two things: range gates in front of every product and quotient, and a kernel
that now decodes the two arithmetic causes. The second is the larger half and it is not in this
directory: [../../kernel/trap.c](../../kernel/trap.c) and
[../../kernel/test/ufpe.c](../../kernel/test/ufpe.c).

## The scale factor overflows before the value does

A BESM-6 float is one 48-bit word: `5.42e-20` to `9.22e+18`, twelve significant digits
(`DBL_MIN`/`DBL_MAX` in the freestanding `float.h`). v7's `getflt()` reads a number by
accumulating the digits, building `10^|dp|`, and dividing once:

```c
	e = 1.;
	i = dp;
	if(i < 0)
		i = -i;
	while(i--)
		e *= 10.;
	if(dp < 0)
		d *= e; else
		d /= e;
```

**Both halves of that fault on values this machine holds perfectly well.** The table's own
`e  1.6021917-19` is 1.6e-19, comfortably inside the range — but `dp` is 26, so the loop
builds `1e26` and dies at `1e19` before the divide ever happens. And `pi 3.14159265358979323846`
never reaches the scaling at all: twenty-one digits accumulated as an integer-valued double is
3.14e20, which overflows in the accumulation.

The port fixes both by refusing to hold a quantity it does not need. Digits stop being
accumulated once there are more than the machine can keep, a dropped integer digit counting as
a place instead; and the scale is applied in gated bites of at most `10^18` from a table, which
is two multiplications for anything in this file and never an intermediate out of range.

## The gates, and why they are gates and not a handler

[../../lib/libm/README.md](../../lib/libm/README.md) states the rule this port had to obey:
*every range gate is placed before the arithmetic that would overflow*, because the fault is
not a value a caller can inspect afterwards — the program is simply gone. So `fmul()` and
`fdiv()` test first and set `fperrc`, which is v7's own flag and the one its
`underflow or overflow` message already printed. Nothing about the diagnostics changed; what
changed is that they can now happen.

The bound is applied to whichever side cannot itself leave the range, which is the only subtle
line in either function:

```c
    if (y >= 1. ? x > DBL_MAX / y : x < DBL_MIN / y) {
```

With `|b| >= 1` a product can only overflow, and `DBL_MAX / y` is computable; with `|b| < 1` it
can only underflow, and `DBL_MIN / y` is. Written the other way round — `x * y > DBL_MAX` —
the test would fault while evaluating itself. A quotient is the mirror, and its `y == 0.` arm
exists because BESM-6 division faults on a zero **or denormal** divisor.

`signal(SIGFPE, fperr)` is still installed, and `units` is the only program on this image that
installs one. With the gates it should never fire; if it does, the handler sets the same flag
and the same message prints, which beats a dead process.

## Six definitions this machine cannot hold

Of v7's 427 definitions, six fall outside the range whatever the arithmetic does:

| | value | |
|---|---|---|
| `mole` | 6.022169e+23 | Avogadro's number, four powers of ten too large |
| `atomicmassunit`, `amu`, `dalton` | 1.66044e-27 | |
| `barn` | 1e-28 | |
| `k` | 1.38047e-23 | Boltzmann's, once `erg` is resolved |

**Nothing else in the table refers to any of them** — `grep -nw` over the file is the check, and
it is why commenting them out costs exactly six definitions and not a cascade. They are
commented rather than deleted, with a note above the first, so the file still records what this
machine cost; asking for one now reports `cannot recognize`.

A table named on the command line may hold such a definition, and `init()` diagnoses it as
`out of range` *name* and **drops** it. Dropping matters: kept with a factor of zero it would
fault the first time somebody put it in a denominator.

## What else the C11 pass turned up

* **`printf("%l units; %l bytes\n\n", i, cp-names)`.** `%l` is not a conversion. `doprnt.c`
  echoes an unknown one verbatim **and consumes no argument**, so this printed `%l units; %l
  bytes` and desynchronised nothing only because there was nothing after it. §3's `%D` hazard in
  a third spelling.
* **`prefix[]` was brace-elided**, and `b6lower` fills an aggregate initializer positionally
  without saying so. Every element has inner braces now.
* `convr(lp)` and `units(tp)` are v7 puns — a `struct table *` passed where a `struct unit *`
  is wanted, the first two members being common. They are casts now and nothing else.

## The measurements

```
	const	text	data	bss	dec	oct
	   99	 3938	 265	4454	8756  21064
```

8,756 words against the 28,672-word ceiling; `rootfs_units_size` is the check. About 3,500 of
the text and 1,030 of the bss are stdio's, so the program itself is small: `table[601]` is
2,404 words and `names[6010]` is 1,002, and those two are most of the bss.

**The stack, which nothing checks.** From the `15 utm 0NNN` prologues in
`build/cmd/units/units.dis`, the deepest chain is

```
main 61 + init 126 + convr 47 + lookup 96 + printf 3 + vfprintf 6 + _doprnt 281 + _flsbuf 112  =  732
```

of 4,096. `init()` is the largest frame in the program and `_doprnt` the largest anywhere in
it; nothing here recurses except `getflt()`, once, on the `|` fraction form.

**On the disk** `units` costs **14 blocks** — ten for the binary, three for the table, one
indirect — and the image went from 228 free to **214**. The manual page was already staged.

## What this harness cannot say

`cmd_units_*` runs the staged program under `b6sim`, where the system calls are the host's, so
**every case names its table on the command line**: the default `/usr/lib/units` would be the
build machine's file (`../look/test/CMakeLists.txt` reached the same rule first). Two facts
therefore have no case, and both were checked by hand on the booted machine:

```
# (echo inch; echo cm; echo 1+30 m; echo mole) | units
431 units; 3153 bytes

you have: you want: 	* 2.540000e+00
	/ 3.937008e-01
you have: underflow or overflow
you have: cannot recognize mole
you have: 
# 
```

That is the real `/usr/lib/units` off the image, the gate reporting instead of faulting, and one
of the six pruned names. The other is the fault itself, which no program on this image can now
reach through `units` — but `awk` can, and this is what the kernel does with it:

```
# awk 'BEGIN{ x = 1000000000; y = x * x; print y * y }'
** SIGNAL 8 **
Floating exception - core dumped
# 
```

Before C18 that line was `panic("trap")` and the machine stopped. Note that `awk`'s own gate is
on *literals*, so `1e18 * 1e18` is refused at scan time with `number out of range`; reaching the
fault takes in-range literals and an out-of-range product.

## What did *not* need saying

**§2 never came up.** `units` holds one byte cursor, `cp` walking `names[]`, and one `char *`
difference, `cp - names`, which happens once at startup. The prefix matcher walks two pointers
but only compares them for equality.

**§11 never came up either.** Nothing here indexes a table by a character, nothing masks with
`0177`, and `<ctype.h>` is not included: the parser tests `c >= '0' && c <= '9'` and switches on
punctuation, so a byte above `0177` in a unit name is simply part of the name and hashes like
any other.

**§5 and §12 do not apply**: `units` opens one file by name and reads no directory and no
device.
