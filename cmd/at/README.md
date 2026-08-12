# at — Unix v7 `at(1)` and `atrun` for the BESM-6

`/bin/at` and `/usr/lib/atrun`, staged by one `b6_prog()` call each in
[`CMakeLists.txt`](CMakeLists.txt) and [`../atrun/CMakeLists.txt`](../atrun/CMakeLists.txt).
5,820 and 6,282 words of the 28,672; **28 blocks of the image between them**, leaving
**169 free** — 12 and 13 for the two programs, each one block over the six direct addresses
an inode holds ([`../../include/sys/param.h`](../../include/sys/param.h), `NADDR`) and so
paying an indirect block as well, and one each for the three directories of the spool. Task
**C21**. One `README.md` for both, because `atrun` is documented inside `at.1` and has no page
of its own.

`at` copies a script into `/usr/spool/at` under a name that encodes when it is due; `atrun`
walks that directory, runs whatever is due, and moves it to `past/`. Two small programs, and
neither would have worked.

## What C21 was blocked on, and why the blocker was mostly imaginary

[`../TODO.md`](../TODO.md) held this task back for a clock, on the grounds that `iinit()`
seeds `time` from the root superblock's `s_time` "and nothing advances it but the interval
timer". Two-thirds of that is wrong, and the wrong two-thirds is the part that mattered:

* **Elapsed time was never the problem.** The interval timer *is* what is supposed to advance
  the clock, and it does: it free-runs at `HZ` 250, SIMH calibrates it against the host's wall
  clock (`sim_rtcn_calb` in `besm6_cpu.c`'s `fast_clk`), and [`../../kernel/clock.c`](../../kernel/clock.c)
  increments `time` on every 250th tick. [`../../kernel/test/uclock`](../../kernel/test/) has
  asserted that increment all along. A program that asks "is it later than X yet" needed
  nothing this kernel lacked.
* **The epoch was always settable.** `date yymmddhhmm` → `stime(2)` → `suser()`. What was
  missing was not a mechanism but an *operator* — and v7's own answer on a machine with no
  battery clock is that somebody types the date in single-user mode.

So the answer was not a kernel task. It is [`../../kernel/date.ini`](../../kernel/date.ini), a
SIMH `expect` action that types the host's wall clock at the first prompt, run by
[`../../kernel/unix.ini`](../../kernel/unix.ini) and by
[`../../kernel/test/multi.ini`](../../kernel/test/multi.ini). What C21 really cost was
noticing that the premise was worth checking. **C22 and C23 are unblocked by the same
finding**, and `../TODO.md` says so now.

The one thing that had to be measured rather than assumed: **`%%VAR%%` does not defer a SIMH
substitution**. SCP expands `%VAR%` when it *reads* a line, so a `send` written into a rule
carries the time the `.ini` was parsed rather than the time the prompt arrived; the documented
fix is to call a nested `DO` file from the action, and it works. `%%` was the obvious
shortcut and an expect action is simply not run through `sim_sub_args` a second time — it
arrives at the console as the literal text `%DATE_YY%`.
[`../../kernel/README.md`](../../kernel/README.md), "Telling it the date", carries the rest,
including the two things that are easy to trip over after it: SIMH's `do` resolves a relative
name against the **working directory** rather than against the calling `.ini`'s, and every SCP
date variable comes from `localtime()`, so a guest that believes it is GMT ends up holding the
host's local wall clock. Neither is worth correcting; both are worth knowing.

## A key schedule for the year, or: 126 is not two digits

**This is the port.** v7 names the spool file `yy.ddd.hhhh.uu` and builds it with

```c
sprintf(fname, "%s/%02d.%03d.%04d.%02d", dir, y, d, t, (getpid()+i)%100);
```

where `y` is `tm_year` — years since 1900. In 1979 that is `79` and `%02d` gives two
characters. In 2026 it is **126**, and `%02d` does not truncate: the name comes out
`126.000.0000.42`. `atrun` reads it back with

```c
sscanf(file, "%2d.%3d.%4d.%2d", &year, &day, &tt, &uniq)
```

which takes `12`, demands a `.`, finds `6`, returns 1 rather than 4 — and the loop's next
statement is `continue`. **Every job would have been skipped, silently, on the first day this
image ran.** No diagnostic, no failed exec, nothing in the spool to look at but a file that
never went away.

The fix is the full year in both places, `%04d` out and `%4d` back, which makes the name
sixteen characters against a `DIRSIZ` of 18. That follows [`../date/date.c`](../date/date.c),
which widened its own year window for the same class of reason and left the argument written
down: on a machine whose `time_t` reaches past 2100, a two-digit year is a bug with a date on
it rather than a convention.

Two consequences came with the wider year:

* **`uyear % 4 == 0` is not a leap rule for a full year.** v7's test was right on years since
  1900 within one century and is wrong on `1900` and `2100`. Both sites use libc's
  **`dysize()`** ([`../../lib/libc/gen/ctime.c`](../../lib/libc/gen/ctime.c)) instead, which is
  the full Gregorian rule and counts years from 1900 as `struct tm` does. `date.c` hit this
  exact trap and is where the habit comes from; the function is declared by no header, so both
  programs declare it themselves.
* **`at.1`'s FILES section had to change**, `yy.ddd.hhhh.uu` becoming `yyyy.ddd.hhhh.uu`. It is
  the only user-visible difference between this `at` and v7's.

## The rest of the diff

**`char **environ;` was a definition, and had to become `extern`.** It is `crt0`'s
([`../../lib/libc/csu/crt0.s`](../../lib/libc/csu/crt0.s)), and §1 of
[`../README.md`](../README.md) names this trap precisely: C11 has no tentative definition
across translation units and `b6ld` has no common symbols, so the file would either fail to
link or — worse — get storage of its own that is permanently NULL. The second outcome is the
dangerous one, because `at`'s use of it is `if (environ)`: every job would have been written
with **no environment at all** and nothing would have said so. `b6nm` over the finished binary
shows one `environ`, and it is `crt0`'s.

**The month table lost its struct.** v7 kept `{ char *mname; int mlen; }` in one array, and
`b6lower` cannot initialize a `char *` inside a struct initializer (§1). Two parallel arrays,
and the loops index rather than walk.

**`utime` is a libc function here**, so v7's global of that name is `attime`. It costs nothing
and it is exactly the collision §1 warns about — `chmod.c` defined `abs()` and `chown.c`
defined `isnumber()` on the same principle.

**`filename()`'s search never terminated.** The step of 53 is prime to 100, so the loop walks
all hundred suffixes and then walks them again, for ever, if every one is taken. It is bounded
at a hundred tries with a diagnostic.

`atrun` also grew the things §5 exists to stop being rediscovered:

* **`opendir(3)` from the start.** v7 `fread`s raw `struct direct` out of a directory it opened
  with `fopen`, skips `d_ino == 0` by hand, and copies each name through a `DIRSIZ+1` buffer to
  terminate it. `readdir()` does all three. That is one fewer entry on C24's list of eight
  hand-rolled readers rather than a ninth, which §5 asks for in as many words.
* **`%.14s` was v7's `DIRSIZ` written into a `sprintf`**, and it was truncating a name that is
  sixteen characters here. It is gone with the `sprintf`: the move to `past/` is `link()` plus
  `unlink()` now, two system calls where v7 forked `/bin/sh` to fork `/bin/mv`. Same directory
  tree and `atrun` is root, so that is the whole of what `mv` would have done.
* **`for (i = 0; i < 15; i++) close(i)`** was v7's `NOFILE`. Ours is **20**, so five
  descriptors were surviving into the job.
* **`execl(..., 0)`** needs a pointer for the varargs sentinel, and the second
  `execl("/usr/bin/sh", ...)` is dropped: there is no such file on this image.
* **The child leaves through `_exit()`.** It inherited the parent's stdio buffers, and `exit()`
  would flush them a second time.

## The hard link, which is a hole and not a wart

`/usr/spool/at` must be mode 0777: `at` is deliberately **not** setuid, so an ordinary user
creates the copy of their own script there and owns it, and that ownership is precisely how
`atrun` recovers the uid to run the job under. A world-writable directory plus "run this file
as whoever owns it" is a well-known shape, and v7 walked into it:

> any user may hard-link a **root-owned** file they can read into `/usr/spool/at` under a
> name that parses, and `atrun` will `stat` it, see uid 0, `setuid(0)` and hand it to
> `/bin/sh`.

`link(2)` does not need write permission on the source, only on the target directory, and the
file's owner comes with it. So this port refuses any job with more than one link. A file `at`
created has exactly one, which is what makes the test free of false positives; the `stat()`
happens before the move, in the parent, where a diagnostic still has somewhere to go. `at.1`
records it under BUGS.

The same reasoning is why **`/usr/lib/atrun` is not setuid**. It needs `suser()` for its
`setuid(2)`, so root must run it — and a bit on it would hand that whole code path to anybody
who could think of a filename.

`past/` is 0777 as well, and not 0755, for a reason worth writing down: `atrun`'s child unlinks
the finished job *after* dropping to the submitter's uid, and `unlink(2)` asks for write
permission on the directory rather than on the file.

## `lasttimedone` is written and read by nothing

`atrun` rewrites `/usr/spool/at/lasttimedone` with `hhmm` on every run, `at.1` documents it,
and **no program in v7 or here ever opens it for reading**. It is kept because the page
describes it; it is recorded here so that nobody goes looking for the consumer.

## Nothing runs `atrun` yet

`at.1` says jobs are run "by periodic execution of the command `/usr/lib/atrun` from cron(8)",
and there is no `cron` on this image — it is task **C22**. So `at` spools correctly and
nothing collects, until somebody types `/usr/lib/atrun` as root. That is honest rather than
broken, and it is the shape the manual page now carries a `Note:` about.

**One thing C22 has to resolve before it can add the line**, and it is v7's own contradiction
rather than anything this port introduced: [`../cron/cron.c`](../cron/cron.c) does `setuid(1)`
in `main`, so a `cron`-forked `atrun` runs as uid 1 — and `atrun`'s `setuid(stbuf.st_uid)` is
gated on `suser()`. As the two stand, a job scheduled by `at` and collected by `cron` could not
be given its owner's identity at all. `../TODO.md`'s C22 section carries the note.

## The assertion

**`test/` here is eight diagnostics and nothing else, and that is the harness rather than a
choice.** Every path in `at` that gets past `makeuday()` opens an absolute
`/usr/spool/at/...`, which under `b6sim` is the **build machine's** (§9). So there is no
`b6sim` case that spools anything: what the eight cover is the time parser (`3z`, `12:6`,
`2500`, `1370`), the two ambiguity tables (`j` is january, june and july; `t` is tuesday and
thursday, and matches no month at all, which is what makes it reach the second table), the day
gate, and the argument count.

**`atrun` has no `test/` at all.** It reads a directory, which `b6sim` cannot do; every path
it names is the image's; and it forks. `mount`, `umount` and `find` are the precedent for a
program that falls wholly outside that world.

### What this harness cannot say

Everything that matters, which is why the real assertion is
[`kernel/test/multi`](../../kernel/test/multi.ini) — stages 13 to 17, on the boot that was
already there, with no new volume number:

```sh
echo 'echo AT${z}RAN >/dev/console' >/tmp/j
at 0000 jan 1 /tmp/j        # midnight on 1 January: already past, so due at once
echo /usr/spool/at/*        # the name, and its four-digit year
/usr/lib/atrun              # ...which parses it, moves it, drops privilege and runs it
```

Two things about that dialogue are deliberate and were both learned the expensive way:

* **The marker cannot be produced by the typing.** The console echoes every character sent, so
  a stage expecting `ATRAN` would be satisfied by the echo of the command that created the
  file — a green test asserting nothing. This is task C20's lesson in a different disguise:
  there the pattern was typed one letter short (`grep updat`), here the *file* says
  `AT${z}RAN` and only the *output* says `ATRAN`, `z` being unset. The year stage is safe for
  the same reason — `2026.000.0000.` appears in no command this test types.
* **It was checked by breaking it.** Narrowing `atrun`'s `sscanf` back to `%2d` makes `multi`
  fail, which is the only way to know that the stage asserting the year is load-bearing rather
  than decorative.

**What is not asserted anywhere**, and is worth knowing before somebody trusts it:

* **A job submitted by a user who is not root.** The dialogue above runs entirely as uid 0, so
  the interesting half of the spool's 0777 — `at` creating a file it does not own, `atrun`
  dropping to uid 7 to run it — has been reasoned about and not exercised. `multi` has a guest
  shell on the second Consul and could carry it; nothing else could.
* **The hard-link refusal.** The guard is two lines and cannot fire in a dialogue where every
  job has one link, so what is tested is that it does not fire wrongly.
* **The environment dump.** It is `HOME=` and `PATH=` here
  ([`../login/login.c`](../login/login.c)'s `envinit`), which is why the job script parses; a
  variable whose value contained a newline would still split into two shell lines, as it did
  in v7.
* **`at` reading its script from standard input**, the form with no *file* argument. Every case
  above passes a filename.
