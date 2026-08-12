# update — Unix v7 `update(8)` for the BESM-6

`/etc/update`, staged as **`build/rootfs/etc/update`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt). 152 words of the 28,672 and one block of the image, leaving
**197 free** (`b6size -w` and `b6fsutil` say so, and both numbers are measured) — the
smallest program on this system after [`/bin/sync`](../sync/), which is its other half: `sync(1m)`
is the flush somebody types, this is the one nobody has to. Task **C20**, and the line it puts back
into [`../../etc/rc`](../../etc/rc) is the first that script has had besides `date`.

The port itself is nothing — 38 lines of v7 with no `long`, no pointer arithmetic and no stdio.
What was worth the task is everything around it, because this is **the first daemon on this
image**: the first program that outlives the shell that started it.

## The line in `/etc/rc` cannot leave two copies running

[`../TODO.md`](../TODO.md) warned that it could, and the warning was wrong. It is true that
`/etc/rc` runs on **every** pass through init's loop rather than once per boot — but the loop is

```c
for (;;) {
    shutdown();  // <-- first
    single();
    runcom();    // <-- /etc/rc
    merge(0);
    multiple();
}
```

([`../init/init.c`](../init/init.c)), and `shutdown()` sweeps the machine with five rounds of
`kill(-1, SIGKILL)` before anything else happens. [`kernel/sys4.c`](../../kernel/sys4.c)'s `kill()`
grants root the `pid == -1` arm unconditionally and skips exactly two slots, `proc[0]` and
`proc[1]` — the swapper and init. A detached daemon in process group 0 is neither, so the copy
started by the previous pass is dead, reaped by `shutdown()`'s `while (wait(0) != -1)` drain,
before the line is read again. **At most one `update` ever runs**, and no path reaches `runcom()`
without a completed sweep first: `setjmp(sjbuf)` is outside the loop, so both the SIGHUP handler
and `shutdown()`'s own 60-second safety net land at the top of the cycle and re-enter `shutdown()`.

It is not killable-in-principle either but killable in fact: `pause()` sleeps at `PSLEP` (40),
which is above `PZERO` (25), so `psignal()` calls `setrun()` and the process takes the signal on
its way out of `sleep()` rather than waiting for something to wake it.

## What it does cost, and it is a state transition

A permanent child changes what `multiple()` can conclude. That loop returns to single-user when
`wait()` gives back −1 — *no children at all* — and with `update` reparented onto init that is
never true again. Two things go with it:

* the `allgone` return to single-user, when the last getty dies, and
* `merge()`'s `nottys` and `nolines` diagnostics, which are *set* where the problem is found and
  *printed* by `single()` on the way past. Init now blocks in `multiple()` instead of coming back,
  so a boot with a missing or unusable `/etc/ttys` says nothing at all.

This is v7's own arithmetic — v7's `rc` started `update` and `cron` on exactly this reasoning — and
it is written down here, and in `multiple()`, rather than fixed: making the loop count gettys
rather than children is a change of substance to init that nothing on this machine needs yet.

## The two changes to v7's source

**The signal dance is gone.** v7 wrote `dosync()` to call `sync()`, re-arm `signal(SIGALRM, dosync)`
— v7 semantics reset a handler on delivery — and `alarm(30)`, with `main()` sitting in
`for (;;) pause()`. That *is* [`sleep(3)`](../../lib/libc/gen/sleep.c), alarm and pause and
`longjmp` and all, so the loop is written out and the dance stays in the library, where
[`lib/test/signals`](../../lib/test/signals.c) already asserts it. `sync()` also stops being called
from a signal frame.

**The three opens are gone.** v7 held `/bin`, `/usr` and `/usr/bin` open "for cache benefit", to
pin their inodes in core. Nothing here measures one: this kernel has 16 buffers and an in-core
inode table to match, and three descriptors held for the life of the system buy a directory lookup
that a machine with two users does not make often enough to notice.

The `fork` stays, and is load-bearing: `runcom()` waits for the shell running `/etc/rc`, so a
daemon that did not detach itself would stop the boot dead. The three `close`s go with it — init
opened `/` three times to fill descriptors 0, 1 and 2 for the script.

It is **not setuid** (§8): `sync(2)` is ungated, and init runs `/etc/rc` as root anyway.

## The assertion

There is no `test/` here. §9 rules it out twice over: a program that does not terminate cannot be a
`b6_progtest` case, and `sync(2)` under `b6sim` would flush the *build machine*.

The home is [`kernel/test/multi`](../../kernel/test/multi.ini), which types the `^D` that gets past
the single-user shell and so is the only surviving test that runs `/etc/rc` at all. Once both users
are logged in, root's shell types `ps -ax | grep updat` and the stage expects `" update\r\n# "`.
Three things about that are deliberate, and the `.ini` repeats them where they bite:

* **`-x` is required.** `update` has no process group and is root's, which is exactly the row
  [`../ps/ps.c`](../ps/ps.c) drops without it.
* **The pattern is typed one letter short.** The console echoes what is typed, so `grep update`
  would put `" update\r\n"` into the stream *before* grep ran; with no daemon grep prints nothing,
  the prompt follows the echo directly, and the rule would match the echo and assert nothing.
* **`<swapped>` is the failure mode to look for** if it ever stalls with the daemon plainly
  running: `ps` prints that instead of the name when the u-area is out. `sched()` only gets there
  after a `swapin()` fails for want of core, which this dialogue never does.

That stage is also what retires the older claim, repeated in [`../README.md`](../README.md) §7 and
in `etc/rc` itself, that nothing asserts the boot script any more. `kernel/test/console` was its
home and is deleted; `multi` inherited the job, and had already been matching the `date` line for
some time without anyone noticing.

`update.8.umm` is the v7 manual page, kept as it was, and it was on the image before the program
was.
