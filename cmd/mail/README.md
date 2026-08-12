# mail — Unix v7 `mail(1)` for the BESM-6

`/bin/mail`, staged by the one `b6_prog()` call in [CMakeLists.txt](CMakeLists.txt) and **setuid
root**, the seventh program on this image to carry the bit. It is **8,935 words** of the 28,672 —
116 const, 6,220 text, 419 data, 2,180 bss — and its deepest stack frame is 281 words of the
4,096 nothing checks. It costs **18 blocks of the image, leaving 140 free**: 15 data blocks, one
indirect block because that is past the six addresses an inode holds directly, **one that is not
the program at all** — `/usr/spool/mail`, the second time this tally has had to count a directory
after task C21's three — and **one that was not budgeted for**: `mail.1` was already on the image
and so looked free, but correcting it took the page from 2,510 bytes to 5,092 and across the
3,072-byte boundary. Task **C28**.

The number was 158 before this task and is 140 after; `b6fsutil` prints it on every build, and
that is where both came from — including the eighteenth block, which the estimate did not have.

## This task began by overturning a refusal, which is the unusual part

`mail` was not an open task. It was the first row of [TODO.md](TODO.md)'s *"Not ported, and why"*
table, and the row gave three reasons and a fourth sentence that turned out to matter more than
any of them. The table's own preamble is the licence: *"Each row is a decision that can be
re-examined; the line count is there so it can be."*

* **"wants a `/usr/spool/mail` directory the manifest has not got"** — true, and it cost one
  `dir` stanza and one block.
* **"a lock protocol built out of the user execute bit"** — true, and it ported **unchanged**.
  `stat(2)` and `chmod(2)` are both here and there is no `flock(2)` or `fcntl(2)` to prefer to
  them, so v7's protocol is not a legacy to be modernised: it is the only one available.
* **"`<whoami.h>` for a `sysname` this tree pruned"** and **"`REMOTE`/`FORWARD` hand the letter
  to `uucp`"** — the only reason that survived, and the section below is what became of it.
* **"There is one user on this machine and nowhere for a letter to go."** This is the one that
  had quietly stopped being true. [../../etc/passwd](../../etc/passwd) carries six accounts, two
  of which get shells, and `kernel/test/multi` has logged **both of them in at the same instant**
  since task 29c — root on `/dev/console`, guest on `/dev/tty1`. There were two users and two
  terminals before there was any way to send a letter between them.

## Dropping uucp did not drop the fork, and the reason is the privilege

v7's `sendrmt()` has two arms. The remote one builds `uux - sys!rmail user` and is gone with
`<whoami.h>`, the `REMOTE` copylet type and the `rmail` dispatch on `argv[0]`. The local one
forks and runs `popen("mail user", "w")`, and **that one stayed** — not out of caution, but
because deleting it would have broken the `m` command in a way that is worth spelling out.

`printmail()`'s first statement is `setuid(getuid())`. By the time a reader can type anything,
this process is no longer root and cannot `chown(2)` a new mailbox or `chmod(2)` the lock bit on
somebody else's. `m user` therefore cannot deliver; what it can do is hand the letter to a
**fresh setuid `mail`**, which starts with the privilege this one gave away. The fork is the
privilege boundary, and v7 reached uucp through the same door only incidentally.

So the diff is smaller than "remove remote mail" suggests: one copylet arm, one header, two
`sprintf`s and the name-splitting loop. `FORWARD` stays, because it is what the surviving arm
writes.

## What the setuid bit is for, and how narrow the window is

Three things `deliver()` does that an ordinary user cannot: append to a mailbox that is 0644 and
someone else's; `chown(2)` a mailbox it has just created to the recipient, which is `suser()`-gated
*precisely* so that a user cannot give a file away; and `chmod(2)` the lock bit on a file
`owner()` would refuse it. Without the bit, `mail` delivers only to yourself, which is not mail.

[../README.md](../README.md) §8 says to ask what call actually needs privilege before reaching for
`04755`, and then asks a second question — what is actually being read — because `ps` and `df`
answered it with a kernel interface instead of a bit. `mail` cannot: it is not reading a value the
kernel could hand over, it is writing a file in somebody else's name.

What makes it defensible is that mail is otherwise **exactly the program you would not want
setuid**: it has a shell escape (`!cmd`), a save-to-any-file command, and a `-f` that opens
whatever it is handed. Every one of those runs after `setuid(getuid())`, which on this system
moves the *real* uid and cannot be undone. The borrowed root lives in `deliver()` and nowhere
else. **Moving that one line would hand root to the shell escape**, which is the same shape of
observation [../mv/README.md](../mv/README.md) makes about `mvdir()` and the reason the manifest
stanza says it too.

`/usr/spool/mail` is **0755 and root's**, and that is the converse of the argument C21 made one
stanza earlier. `at` is *not* setuid, so a user creates their own spool file and the directory has
to be world-writable; `mail` **is**, so root creates every mailbox and nobody needs write
permission on the directory at all — which also means nobody can unlink anybody else's mailbox,
`unlink(2)` asking for write permission on the directory rather than the file.

## The rest of the diff

* **`lock()` had to be renamed, and not for taste.** `lock` is a real symbol in this libc — the
  `lock(2)` stub at [../../lib/libc/sys/syscalls.tbl](../../lib/libc/sys/syscalls.tbl), with a
  manual page of its own. This is §1's `abs()`/`isnumber()` case exactly: nothing fails to link,
  the collision simply waits for the day something pulls that object in. `index` is the same.
* **The child called `exit(0)`.** It inherits the parent's stdio buffer, so `exit`'s flush prints
  the parent's pending output a second time; `_exit` is the rule
  ([../../lib/libc/stdio/popen.c](../../lib/libc/stdio/popen.c)).
* **Catching every signal was worse than sloppy.** v7 armed 0 through 19 — `ssig()` refuses 0,
  `SIGKILL` and everything past `NSIG` (17), so a third of the calls did nothing. The harm was in
  the ones that worked: a caught `SIGSEGV` or `SIGFPE` turned a genuine fault into a `longjmp`
  back to the `? ` prompt, so a bug became an endless prompt and no core file. Four named signals
  now. **`SIGPIPE` was a live bug** on top of that: in the forked child it would have run the
  handler, jumped to `main`'s `setjmp` *in the child*, and unlinked the **parent's** temp file.
* **Three unbounded copies, where §6 names one.** `-f`'s `strcpy(mailfile, argv[2])` into 40
  bytes is the documented one; `getarg()` copies unbounded from a 256-byte reply into a 50-byte
  name, and the deleted remote arm did the same into `rsys[64]`. The `-f` case is not
  hypothetical — this directory's own tests hand it a path longer than that.
* **`let[MAXLET]` had no bound and was one short.** `copymt()` writes `let[nlet]` after
  `let[nlet++]` and `copyback()` writes `let[++nlet]`, so a mailbox of exactly 300 letters stored
  past the end, and 301 walked off into bss. It is now two arrays of `MAXLET+2`, which is also
  248 words cheaper than the struct — a `char` member rounded each entry up to two words.
* **`copyback()` printed the wrong variable** in its `can't rewrite` diagnostic: `lfil`, which
  holds whatever file the last `s` command named, rather than the mailbox.
* **`mail -z` exited 0** after refusing the option. It exits 1, which is what lets `.status` mean
  anything.
* **`setjmp` and automatics.** `i`, `j` and `print` are assigned after `setjmp` and read after a
  `longjmp`, which C11 leaves indeterminate unless they are `static` or `volatile`; v7 had no such
  rule. There is also a window v7 left open at `donep:` — `sjbuf` belongs to `printmail`'s frame,
  so a signal arriving after it returns would jump into a dead one. Clearing `delflg` there makes
  the handler finish instead.
* **`time((long *)0)`** is a constraint violation here even though `long` and `time_t` are both
  one word: incompatible pointer types. `long adr` became `off_t` for the same reason.
* **`umask(~0644)`** worked only because [../../kernel/sys4.c](../../kernel/sys4.c) saves
  `mask & 0777`, which turns a negative word into 0133. The number is written down now.
* **`struct passwd` is one shared buffer**, and `<pwd.h>` says so in capitals. Neither use was
  corrupt — nothing between `getpwnam()` and `chown()` calls the `getpwent` family, and I traced
  the lock path to be sure — but `my_name` held `pw_name` for the entire run, which is the same
  hazard with a longer fuse. Both copy out now.
* **§11 needed nothing.** There is no `ctype` call, no `&0177`, and no table indexed by a
  character anywhere in the 556 lines; `copylet()`'s copy is bounded by a byte count rather than
  by a `c > 0` test, so a `0377` in a letter survives. What that wanted was not a fix but a
  positive case, and the fixture carries a Cyrillic letter for it.
* **`-f` no longer locks.** The one deliberate departure from v7, argued in
  [test/CMakeLists.txt](test/CMakeLists.txt) and noted in the page: the protocol is about
  `/usr/spool/mail`, and locking a private `mbox` only changes the mode of a file of your own.

## The assertion

Seventeen `b6_progtest` cases in [test/](test/), which is more than any port here has had, and
the reason is that `mail` is the first one whose `b6sim` half can exercise real *output* rather
than only diagnostics: `-f` lets a checked-in mailbox be read without any of the machine's own
state being involved. `printall` and `forward` are the same fixture read in both directions and
are each other's control, which is §9's designed-fixture oracle.

The fixtures are **copied into the build directory and read by a relative name**, which no other
test directory here does. [test/CMakeLists.txt](test/CMakeLists.txt) gives both reasons; the
general one is worth lifting out, because it is not about mail:

> **A `b6_progtest` case whose program can write to its own input file cannot read that file out
> of the source tree.** Every mutating command in `mail` ends in `copyback()`, which rewrites
> whatever `-f` named — so one `d` in a `.in` file would edit the fixture and every later run
> would test a different mailbox.

Every interactive case therefore ends in `x` or `q`, and none of them reaches `copyback()`.

### What this harness cannot say

* **That a letter ever arrives.** `/usr/spool/mail/<user>` under `b6sim` is the build machine's,
  and it is not there. Every delivery path in the suite ends in a diagnostic, and `deliver()`'s
  success arm — `fopen(…,"a")`, `chown`, `copylet` — is run by nothing in it.
* **That the setuid bit does anything.** `b6sim` runs the a.out directly and never reaches
  `getxfile()`'s ISUID branch. `rootimg_setuid` asserts the bit is *on the image*, which is a
  different claim.
* **That the lock works.** It is a property of two processes contending, and there is one.
* **That `copyback()` is correct** — see the constraint above. `d`, a successful `s` or `w`, and
  the "new mail arrived" re-read are untested here.
* **That `m user` or `!cmd` work.** Both end in an exec of `/bin/sh`, and `b6sim`'s exec loads a
  BESM-6 a.out.
* **That an interrupt stops the printing of a letter**, or that `-q` exits on one.
* **That `my_name` is ever a login name.** `/etc/utmp` is not among the six files
  [../sim/etcfiles.cpp](../sim/etcfiles.cpp) serves, so `getlogin()` answers NULL and
  `getpwuid()` is asked with the *build machine's* uid. Every case runs as `???`, and no
  expectation may contain a user name for that reason.
* **That `login` prints `You have mail.`**

### What was checked by hand instead

Task C28 took decision 3 — no new booting test — so this is the record that the other half was
checked. Under `make demo`, with the dialogue driven the way `kernel/test/multi` drives its two
Consuls:

```
# echo hello from root | mail guest
# ls -l /usr/spool/mail
total 3
-rw-r--r--  1 guest          52 Jul 25 08:23 guest
```

…and then, on the second Consul, `guest` logging in:

```
You have mail.
$ mail
From root Sat Jul 25 08:23:01 2026
hello from root
```

Four things are in those nine lines and none of them is available to `b6sim`: the setuid bit was
honoured, `chown(2)` ran and **the mailbox belongs to `guest` rather than to root**, `MAILMODE`
produced 0644, and `login`'s `access()` probe — written in task C6 and answered by nothing since —
printed `You have mail.` for the first time.

Still unchecked by anything, and named here rather than left to be discovered: two `mail`
processes racing the lock, and `^C` during a long letter.
