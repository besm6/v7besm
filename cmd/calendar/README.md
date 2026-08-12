# calendar — Unix v7 `calendar(1)` for the BESM-6

Task **C23**. Two files on the image and neither is the whole program:
[`/usr/lib/calendar`](calendar.c) prints an `egrep -f` pattern for today and tomorrow, and
[`/usr/bin/calendar`](calendar.sh) is the shell script that greps with it. That division is
v7's, and it is why the halves live in different directories. A third thing is new here: the
eight-file database under `/usr/lib/calendars`, which v7 had not got.

`/usr/lib/crontab` has carried a commented-out `20 1 * * * /usr/bin/calendar -` line since task
C22 with the note *"calendar is task C23 and is not ported yet"*. It is live now.

## The database did not match v7's own pattern, and eleven months of the year were the proof

v7's generator emits, for 6 January:

```
(^|[ (,;])(([Jj]an[^ ]* *|1/)0*6)([^0123456789]|$)
```

A leading zero is tolerated on the **day** — `0*6` — and not on the **month**. Every line in
[`calendars/`](calendars/) is written `01/06<tab>Epiphany`, so for January through September the
pattern matched nothing at all; October, November and December worked by the accident of having
two digits already. The port's one deviation from v7 is `0*%d/` in place of `%d/`, which is one
character, and it buys the whole database plus every user who writes `01/06` meaning the sixth
of January.

Checked before it was written, with the image's own `egrep` under `b6sim`: v7's pattern against
`calendar.christian` finds nothing for January and finds `10/18 Feast Day of St. Luke` for
October; the fixed one finds `01/06* Epiphany`.

## `case` used to crash this shell, and that is why the script is shaped as it is

v7's script is a `case $# in 0) … *) … esac`. Written that way it does not work here. Running
`calendar -` on the booted machine produced `** SIGNAL 8 **`, then the shell's own message
tables and variable list printed to the console as text, then `cannot shift` — a diagnostic
from a builtin the script never calls. Under `multi` the same thing arrived as a string of
`** SIGNAL 11 **`, and root's login shell died with it.

Bisected on the real machine, one construct at a time, all of them at a live prompt:

| typed | result |
|---|---|
| `sed … /etc/passwd \| while read x; do eval $x; echo Z$z; done` | six lines, correct |
| `echo a \| while read x; do echo $x \| cat; done` | correct |
| `echo a \| while read x; do if test -r /etc/motd; then echo $x \| cat; fi; done` | correct |
| `echo z=q \| while read x; do eval $x; echo $z \| cat; done` | correct |
| `echo a \| while read x; do egrep a /etc/motd \| cat; done` | correct |

Every piece works. What fails is the assembly, and only under one more level of nesting:

* `case` arm → `while` → `if` → pipeline: **the shell corrupts itself** (`SIGNAL 8`, garbage
  on the console, `cannot shift`).
* `case` arm → `while` → pipeline, with the `if` taken out: **hangs**, and `calendar -` never
  returns.
* `if`/`else` → `while` → `if` → pipeline — the same code one level shallower: **correct**,
  and it is what [`calendar.sh`](calendar.sh) ships.

Moving `;;` onto a line of its own changes nothing, so it is not a `fi;;` tokenising problem.

**That reading was right, and task C29 fixed it.** The shape of the failure — a corrupted data
segment rather than a syntax error — and the fact that a single nesting level decided it were the
recursion `execute()` does over the parse tree against [`../sh`](../sh)'s **4,096-word stack**:
one 402-word frame per level and eight levels of budget, with the overflow wrapping onto word 0
rather than faulting. `execute()` is split into one function per arm now, the frame is 99 words,
sixteen levels fit, and `deepchk()` refuses the seventeenth out loud.
[`../sh/README.md`](../sh/README.md), "The stack, and what a nested script costs", is the account.

**So the `case` form would work today.** [`calendar.sh`](calendar.sh) keeps its `if`/`else`
anyway: it is one level shallower for no cost, it is what `kernel/test/multi` stages 18 to 22
have been asserting since C23, and the shell's own regression tests are where the shape belongs —
`kernel/test/multi` types the bare repro at root's prompt, and `sh/test/nest` nests twelve deep
under `b6sim`.

**Why no test caught it before.** Two reasons, and the second is the one that matters: the
b6sim harness runs `env -i`, and a login shell's environment sits at the base of the very stack
that ran out. This script measures 3,737 words of 4,096 with an empty environment — it would have
passed under `b6sim` and it died on the machine.

## What the fallback is for

v7's `calendar` reads the file `calendar` in the current directory and nothing else. That is
still true here when there is one; when there is not, this version greps `/usr/lib/calendars/*`
instead, so a bare `calendar` on a machine where nobody has written one still prints something.

It was **measured** rather than guessed. A four-file subset — computer, usholiday, christian,
judaic — holds entries for 114 days of the year, and a fallback silent two days in three is a
demonstration and not a service; all eight cover 362 of 366, and cost twenty-nine blocks. The
image had 140 free and has 98.

`egrep -h`, because the fallback names eight files at once and `egrep` prefixes each line with
the filename otherwise.

**The `-` form does not fall back**, deliberately: a user is mailed his own reminders or none,
and the alternative is mailing every user on the machine the same system file every morning.

## The rest of the diff

* The §1 C11 pass: prototypes, `int main(void)`, `struct tm *localtime();` gone.
* `long t` is `time_t t` and `#define DAY (3600*24L)` loses the `L` — a `long` is one word here
  (§3).
* The script's `sed` expression is on **one line**. v7 spreads it over three inside single
  quotes, with a `\` continuation before the pipe. That turned out not to be what was breaking
  anything, but a one-line expression is what the rest of this tree writes.
* `${LIBCAL-/usr/lib/calendar}` and `${CALDIR-/usr/lib/calendars}`, in
  [`../lorder/lorder.sh.in`](../lorder/lorder.sh.in)'s `${NM-nm}` idiom and for the same reason:
  they are what lets [`test/`](test/) drive the shipped script. No colon — `:` is not a
  substitution character in [`../sh/ctype.h`](../sh/ctype.h).
* No `#!` line, and mode 0755. `/usr/bin/lorder` is the precedent and
  [`../../scripts/root.manifest`](../../scripts/root.manifest) has the argument: there is no shebang on this
  system, and the shell reads a file back itself when `exec` returns `ENOEXEC`.

## The assertion

Two harnesses, because neither can do the other's half.

**[`kernel/test/multi`](../../kernel/test/multi.ini), stages 18 to 22**, on the booted machine.
It is date-independent without knowing the date: the fixture is one line written with the same
`%DATE_MM%` and `%DATE_DD%` that stage 1 typed at the clock, and **the month is zero-padded**,
so the line would be missed for eleven months of the year without the deviation above. Both
forms are there — the plain one prints the reminder, and the `-` one writes `/home/guest/calendar`
and then finds the letter in `/usr/spool/mail/guest`. The marker is broken by a quote —
`CAL'OK'` typed, `CALOK` matched — because the console echoes what is typed; stage 13's
`AT${z}RAN` is the same trick.

That last stage is worth more than its two lines. [`../mail/README.md`](../mail/README.md)'s
"What this harness cannot say" opens with ***that a letter ever arrives***: task C28 checked
delivery by hand and left it unasserted, because it took the decision not to add a booting test.
This is the first thing in the tree that asserts it.

**[`test/`](test/)**, three GoogleTest cases on the host, in
[`../lorder/test/`](../lorder/test/)'s shape — a shell script has no engine to link, so the test
drives the shipped file under the host shell. The generator is the **real** one, run under
`b6sim` through a wrapper the test writes, so what travels the pipeline is the staged binary's
own output. The fixture holds every date of the year and the test works out which lines must
come back: one per day the generator covers, four on a Friday and three on a Saturday.

### What this harness cannot say

* **That the system database on the image is ever read.** The fallback is asserted over a
  fixture directory in `test/`, not over `/usr/lib/calendars` — which day of the year the run
  lands on decides whether the real files have anything to say, and eight files covering 362
  days still leave four that do not.
* **That `cron` ever runs it.** `multi` types `calendar -` itself. Nothing waits for 01:20, for
  the same reason [`../cron/README.md`](../cron/README.md) gives for not waiting for `atrun`:
  the guest clock is calibrated to the host's, and a stage that waited would turn a
  five-second dialogue into an all-night one.
* **That a user other than `guest` is ever reached.** `guest` is the only one of the six lines
  of `/etc/passwd` with a home directory this image has and a `calendar` in it — `/home/guest`,
  the sole tenant of `/home`.
* **That the temporary file is cleaned up on a signal.** The `trap` covers 0, 1, 2, 13 and 15,
  and nothing sends any of them.

### What was checked by hand instead

`calendar -` in **single-user**, which is where the shell crash was found and where the fix was
confirmed before `multi` was touched at all — one shell, no daemons, no second terminal, so
that neither the crash nor the fix could be blamed on load:

```
# echo '08/12 MAI'OK >/home/guest/calendar
# /usr/bin/calendar -
# grep MAI'OK' /usr/spool/mail/guest
08/12 MAIOK
```
