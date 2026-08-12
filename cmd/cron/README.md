# cron — Unix v7 `cron(8)` for the BESM-6

`/etc/cron`, staged by one `b6_prog()` call in [`CMakeLists.txt`](CMakeLists.txt), with
[`crontab`](crontab) beside it copied to `/usr/lib/crontab`. **3,845 words** of the 28,672
(`b6size -w`: 74 const, 2,515 text, 203 data, 1,053 bss — a thousand of the bss and most of the
text are stdio's) and **ten blocks of the image**, which leaves **158 free**: eight for the
program, one past the six direct addresses an inode holds and so paying an indirect block as
well; one for the crontab; and one more for the paragraph this task added to
[`../../etc/rc`](../../etc/rc), which pushed that file from two blocks to three. Task **C22**.
The number before this task was **168** and the tree said 169; both are `b6fsutil`'s, and the
one that had drifted was the written-down one.

This is the **second daemon** on the image, after [`../update`](../update/) (C20), and the first
program here that runs *other* programs on a schedule. It also finishes C21: `at(1)` has claimed
since it landed that jobs are collected "by periodic execution of the command `/usr/lib/atrun`
from cron(8)", and until now nothing did that but a person typing the name.

## The question C22 had to answer before it could write a line of it

v7's `cron` calls **`setuid(1)`** as the first statement of `main`, and `atrun`'s
`setuid(stbuf.st_uid)` — the call that gives a job the identity of whoever submitted it — is
gated on `suser()`. So a `cron`-forked `atrun` runs as uid 1 and cannot do the one thing it
exists to do, and the crontab line `0,5,10,… /usr/lib/atrun` is exactly that composition.
[`../README.md`](../README.md) called this C22's first question rather than an implementation
detail, and it was right to.

**The `setuid(1)` is gone.** Three things make that the cheap answer rather than the lazy one:

* **The privilege it drops is not the one that matters.** What decides whether a `cron` job can
  do harm is who may write the crontab, and `/usr/lib/crontab` is mode **0644 and root's** in a
  0755 `/usr/lib`. Nobody but root can put a command in it, so running root's own commands as
  root hands nothing to anybody. The file's mode is part of the design now, and the manifest
  stanza says so.
* **uid 1 is nobody here.** [`../../etc/passwd`](../../etc/passwd) has `daemon:x:1:1::/:` — an
  unmatchable hash, no shell, and not one file on the image is owned by it. v7's drop was to an
  account with a life of its own; this one is to a number.
* **The alternative was refused two tasks ago.** A setuid bit on `/usr/lib/atrun` would hand
  its `setuid(2)` path to anybody who could think of a filename, and
  [`../at/README.md`](../at/README.md) is where that is argued. Nothing about C22 reopens it.

What is left is written into the page, the manifest and `/etc/rc` rather than only here,
because it is the sort of fact somebody will want at the point of use: **every crontab command
runs as the super-user.**

## v7's `init()` was broken three ways, and only the third bites this image

`init()` compiles the crontab into one `malloc`'d block, with `cp` walking it and `ocp` marking
the start of the current line so a bad line can be rewound. Reading it against C11 and against
[`../../lib/libc/gen/malloc.c`](../../lib/libc/gen/malloc.c):

1. **`free(list); list = realloc(list, n);`** — twice, once on the re-read path and once on the
   grow path. That is use-after-free, and [`../ed/ed.c`](../ed/ed.c) had already settled the
   house rule for it: plain `realloc`, no `free` first. v7's shape only ever worked because
   nothing called `malloc` in between, which is not even true here — `_filbuf` allocates the
   stdio buffer inside this very function.
2. **`cp = list + (cp - olist)`** differences a pointer that has just been freed. It gives the
   right number, because `olist` was captured before the `free` and the grow happens at the top
   of a line where `ocp` has not yet been set — and it stops giving the right number the moment
   the block has to grow in the *middle* of a line, which is what fixing (3) requires.
3. **The bound test runs once per line and the stores do not.** `if (cp > list+listsize-100)`
   guarantees a hundred bytes of headroom at a line start; after it, five fields, an unbounded
   comma list and an unbounded command string are written with nothing checking. **A record past
   a hundred bytes — five `*` fields and a command of ninety-odd characters, which is an
   ordinary command with arguments and a redirect — writes through the end of the block**, and
   an arena block here carries its busy bit in its header word, so what it corrupts is the
   allocator. The shipped crontab does not reach it: the live line compiles to 34 bytes. So this
   is invisible on this image and certain on the first crontab an administrator writes.

The rewrite is one idea: **`cp` and `ocp` become `int` offsets into the block, and every byte
goes in through one `put()` that grows it.** §2 of [`../README.md`](../README.md) is careful to
say that turning a cursor into an index buys nothing on this machine — `list[i++]` is a `b$padd`
exactly as `*cp++` is a `b$pinc` — and that is fine, because speed is not the argument. **An
offset survives a `realloc` and a pointer does not.** `listlen = linebase` is the rewind, and it
is correct however many times the block moved while the line was being compiled.

Two things `put()` has to know about this allocator, both of which its comment carries:
`realloc(NULL, n)` **is** `malloc(n)` here, which is why v7's `if (list) … else …` prologue
disappears entirely and `list` simply starts NULL; and a `realloc` that **fails has already
freed the old block** — it does `free(p)` and then `malloc` — so the pointer must be nulled with
it, or the next pass compiles into a block the arena has handed to somebody else.

## A daemon that forks other programs cannot leave 0, 1 and 2 closed

[`../update`](../update/) closes all three and that is right for it: it never forks. `cron`
forks arbitrary programs, and a job inheriting three free descriptors gets them back from its
own first `open(2)` — its data file silently becomes somebody's standard output.

v7 knew this and answered it oddly: it held the root **directory** open read-only on 1 and 2, so
every write failed, and `ex()` did a third `freopen("/", "r", stdin)` before each exec. That last
one reads like v7 giving a job a standard input, and it is not — it is plugging the hole
`init()`'s own `fclose(stdin)` opened, because v7 read the crontab through `stdin`.

This port opens `/dev/null` `O_RDWR` on all three after a `NOFILE`-wide sweep, exactly as
[`../atrun`](../atrun/) does, and reads the crontab through a `FILE` of its own. So descriptor 0
is never closed, `ex()` loses a call rather than gaining one, and `cron.8`'s promise that a job's
output is lost unless redirected becomes true instead of nearly true. The sweep is `NOFILE`
(20) wide and not three deep for `atrun`'s reason: v7's hard-coded 15 let five descriptors
through, and `/etc/rc`'s shell need not have left exactly three open.

## There is nobody to tell

The consequence of all that is worth stating on its own, because it shapes the rest of the file:
**`cron` has no diagnostic path at all.** Descriptor 2 is the bit bucket by construction, and the
program contains no `printf` of any kind. So every failure is a control-flow decision rather
than a message: `init()` grew a return value v7's has not got, `put()` carries a sticky flag
instead of propagating one through five call sites, and `main` responds to a failed compile by
resetting `filetime` and skipping the minute — which is also what guarantees the invariant the
job loop rests on, that `list` is never walked after an `init()` that may have freed it.

## The rest of the diff

* **`isdigit()` in `number()` is §11's `m4` bug in a fresh program.** Its argument is `getc`'s
  result, and [`../../lib/libc/gen/ctype_.c`](../../lib/libc/gen/ctype_.c) is 129 entries indexed
  `(_ctype_ + 1)[c]` — right at EOF and off the end for any byte above `0177`. A stray `_N` read
  out of whatever follows the table would make the loop eat the file. It is an explicit
  `'0' .. '9'` test now, and it is the only character-indexed thing in the program.
* **A field with no digits at all returned 0.** v7's `number()` finds nothing, returns `0`, and
  `1,,2` compiles as though a zero had been written. It returns −1 now. The one place that
  behaviour was load-bearing is the comment line — `#` is not a digit, so v7 fell all the way
  through to `ignore` two functions away — and the `#` test is explicit here instead, placed
  after the five fields so that a *command* may still begin with one.
* **`cstat.st_mtime > filetime`** never notices a crontab restored from a backup or written by
  something that preserves timestamps. `!=` costs the same call.
* **The sentinels are pinned to the value range.** `EXACT` … `EOS` are 100–104 and a field value
  is 0–99, and the only thing keeping them apart is `number()`'s own bound. A
  `_Static_assert` ties the two constants together, so widening the range breaks the build
  rather than the matcher.
* Small change, `atrun`'s precedents each: the two `exit(0)`s in `ex()`'s forked children become
  `_exit()`, the stdio buffers being inherited; `execl`'s varargs sentinel needs `(char *)0`;
  `wait(&st)` becomes `wait((int *)0)`, the status never having been examined.
* **A plain `char` would break the parser outright.** It is unsigned here, so `char c = getc(cf)`
  folds EOF to `0377` and the command loop never ends. v7's implicit `int` saved it by accident;
  C11 makes it a decision.
* **Eight-bit command text passes through.** The only tests applied to a command byte are
  `== '\n'`, `== '%'` and `== EOF`, and `/bin/sh` is byte-transparent (§11), so a Cyrillic
  command works end to end. The one byte that *is* interpreted, `%` → newline, is ASCII.

The double fork in `ex()` stays and is v7's: the middle child leaves at once, so the `wait()`
returns at once and the job is orphaned onto init. A loop that has to wake every minute must not
wait on a job that runs for an hour. The `fork` in `main` stays for `update`'s reason —
`runcom()` ([`../init/init.c`](../init/init.c)) waits for the shell running `/etc/rc` — and the
C20 argument that a daemon in a re-run script cannot accumulate transfers unchanged: `shutdown()`
sweeps with `kill(-1, SIGKILL)` before `runcom()` on every pass, and `cron` ignores HUP, INT and
QUIT but not KILL.

One inherited fact that is invisible from a job's own source: **`SIG_IGN` survives `exec`**, so
every crontab command — and `atrun`, and everything `atrun` starts — runs with SIGINT and SIGQUIT
ignored. That is v7's, and it is what a detached job wants.

## The assertion

There is no `test/` here, and §9 rules one out three times over: a program that does not
terminate cannot be a `b6_progtest` case; `/usr/lib/crontab` under `b6sim` would be the **build
machine's**, the simulator serving only the six static `/etc` files; and it forks.

The home is [`kernel/test/multi`](../../kernel/test/multi.ini), stage 12b, and it is C20's stage
in a new suit: `ps -ax | grep cro`, expecting `" cron\r\n# "`. Both of that stage's lessons
apply unchanged — `-x` is required because `cron` has no process group either, and **the pattern
is typed one letter short** so that the console's echo of the command cannot satisfy the rule.

### What this cannot say

* **That `cron` ever runs anything.** The stage proves the `/etc/rc` line started a process that
  survived, and no more. **The reason is worth the sentence, because it is not what it looks
  like**: the whole `multi` dialogue — boot, two logins, an `at` job spooled and run — takes
  **about five seconds of wall clock**, and the guest clock is not fast, it is *calibrated* to
  the host's (`sim_rtcn_calb`), so five seconds of wall clock is five seconds of guest time.
  Waiting for the live crontab line therefore means waiting for a real five-minute boundary and
  would make a five-second test a five-minute one.

  **So it was checked by hand once, and this is the recipe**, because the trick that makes it
  cheap is not obvious: **type a date whose minute is 04**, so that the next boundary the
  crontab names is under a minute away rather than up to five.

  ```
  # date 2608120404                                  <- at the single-user prompt
  ^D                                                 <- into multi-user; /etc/rc starts cron
  # echo 'echo CRONRAN >/dev/console' >/tmp/j
  # at 0000 jan 1 /tmp/j                             <- due at once, and then NOTHING is typed
  ```

  At 04:05:00 `cron` woke, forked `/usr/lib/atrun`, and `atrun` found the job, moved it to
  `past/`, dropped to its owner and handed it to `/bin/sh`; `CRONRAN` came out on the console
  **sixty-one seconds after the simulator started**. That is the whole chain — the `/etc/rc`
  line, the crontab parse, the minute match, the double fork, and C21's two programs — and it is
  the only time all of it has run together. A minute-scale variant that exercises the re-read on
  `st_mtime` as well is to append `* * * * * echo hello >/dev/console` to `/usr/lib/crontab`
  from the shell and wait.
* **The grow path**, which is the one thing that was actually broken. The shipped crontab
  compiles to 34 bytes and never reaches even the first 512, so the rewrite rests on being
  correct by construction. By hand: put a two-hundred-character command in the crontab and
  watch it run.
* **A `#` comment, a reversed range, an eight-bit command, and the `%`-to-newline translation.**
  None of them appears in the shipped file except the comments, and the comments are proved only
  by the live line still working.

### And one race it introduces, small and worth knowing

`cron` forks `/usr/lib/atrun` on five-minute boundaries, and `multi`'s stages 13 to 17 spool an
`at(1)` job and then type `/usr/lib/atrun` themselves. A scheduled run landing between the two
would collect the job early and stall stage 15 or stage 17. It needs the five seconds of the
dialogue to straddle a multiple of five minutes *and* the boundary to fall in the half-second
between the spool and the hand-run — a couple of runs in a thousand. It is written into
`multi.ini` beside those stages rather than designed around, so that a one-off failure there is
recognised instead of investigated.

## Clock jumps, which this image will see

The operator types `date` in single-user mode, *before* `/etc/rc`, so `cron` normally starts on a
corrected clock ([`../../kernel/README.md`](../../kernel/README.md), "Telling it the date").
Anyone who runs `date` afterwards should know what it does: forward, `cron` walks one minute at
a time with `slp()` returning immediately and **executes every intervening minute's jobs**;
backward, it sleeps until real time catches up and does nothing at all in between. Both are
v7's, and neither is worth changing on a machine whose clock is set by hand.
