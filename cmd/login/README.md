# `login`, and the first shell on this machine that is not root's

Kernel task 29b's second program. The C11 pass is [login.c](login.c)'s own header; this is what
the port taught, and the first section is the reason [cmd/README.md](../README.md) §8 had to be
rewritten.

## Every shell before this one was root's

`cmd/README.md` §8 said it plainly: `getxfile()` honours the setuid bit only for a caller that
is not already root, and *nothing on this image was ever anything else*. `init` exec'd `/bin/sh`
directly, so the console shell was uid 0, and every command it ran was uid 0. The single
exception was [lib/test/suidt](../../lib/test/suidt.c), which drops to uid 7 on purpose to prove
the bit works at all — an experiment, run by a program written for the purpose.

`login` is where that stops being an experiment. `getty` execs it as root, it checks the
password, and `setgid()`/`setuid()` hand the terminal to whoever answered. `kernel/test/login`
watches the shell prompt change from `# ` to `$ `, which is [cmd/sh/main.c](../sh/main.c)
picking `ps1` off `getuid()` and is the shortest possible statement that this really happened.

**The order of the privileged calls is the whole of the security, and it must not be tidied.**
`chown(2)` is `suser()`-gated in this kernel ([kernel/sys4.c](../../kernel/sys4.c)), so
`chown(ttyn, ...)` has to run while this is still root — before `setgid()`/`setuid()`, not
after. Moving that line down gives a `login` that "works" and leaves every terminal owned by
root. Nothing in the guest can see the difference, which is why `run-login.sh` asks the host:
`/dev/console` owned `7/3` after the run is the one assertion in the tree that login's single
privileged call happened.

**It is not setuid**, and that is not an oversight. The privilege is *inherited* — `getty` is
`init`'s child and already root — rather than borrowed, so 0755 is what the v7 reference tree
ships and what [root.manifest](../../scripts/root.manifest) says. v7's manual page claims `login` "may
be used at any time to change from one user to another"; that use needs the setuid bit, and the
command for it is `su`, which is task C6's.

## A prompt that was never printed

The first bug the port hit is not v7's and is not this machine's — it is this **libc's**, and it
will catch every later program that prompts.

```c
printf("login: ");
fflush(stdout);          /* not v7's */
```

v7's stdio went **fully unbuffered** on a terminal, spending a `write(2)` per character.
[lib/libc/stdio/flsbuf.c](../../lib/libc/stdio/flsbuf.c) added a mode v7 had no notion of and
made stdout **line buffered** there instead — which is what C11 asks for and much the cheaper of
the two. But a prompt has no newline, and nothing ends the line: the next thing `login` executes
is a read, and there is no tie between `stdin` and `stdout` here to flush on its behalf.

The symptom is not a diagnostic. `getty` writes the *first* `login:` with `write(2)`, so the
prompt appears and the machine looks healthy; it is the **second** one — login's own, after
`Login incorrect` — that never arrives, and the terminal simply goes quiet. Every other message
in the program ends in `\n` and needs nothing.

## The rest of the diff

* **§2, the pointer comparison.** `namep < utmp.ut_name+8` bounded the name buffer with a
  relational between two `char *`, which did not order them when this was ported. It is an
  `int` index now. The same bug class had made `getpass()` — which this program is the
  **first caller of** — return the empty string for months (`lib/libc/README.md`).
* **§3, the longs.** `lseek(f, (long)(t*sizeof(utmp)), 0)` and `lseek(f, 0L, 2)`: `off_t` is one
  word, so the casts say nothing and are gone, and the whences are spelled `SEEK_SET`/`SEEK_END`.
* **The K&R declarations at the head had to go** rather than be modernised — `<pwd.h>` declares
  `getpwnam()` and `<unistd.h>` declares `ttyname()`, and a second declaration of a different
  shape is an error, not a redundancy. `crypt()`, `getpass()` and `ttyslot()` were the other way
  round when this was ported — no header declared them, so all three were written out here, as
  `lib/test/pwent.c` and `lib/test/ttyt.c` wrote them out too. **Task C6 ended that**: they are in
  `<unistd.h>` now, beside `ttyname()`, because five more callers were about to copy the same
  block. The four copies are gone and this source declares nothing.
* **An upstream bug.** v7 passed `utmp.ut_name` — a `char[8]` that `strncpy` leaves
  *unterminated* for a name of exactly eight characters — straight to `getpwnam()`, which then
  read on into `ut_time`. The name goes through a nine-byte local here and is copied into the
  record; the record still holds v7's eight unterminated bytes, because that is what
  `/etc/utmp`'s format is.

## What it reaches that nothing had reached before

`login` goes further into libc than anything else in `cmd/`, and three of the routines it calls
had never been executed on this machine at all:

| | |
|---|---|
| `getpass()` | never called. Its `char *` bound was wrong until the termcap port found it. |
| `ttyslot()` | always returned 0, because `/etc/ttys` did not exist. |
| `/etc/utmp` | never written by anything. `init`'s `merge()` creates it; this writes the record. |

`crypt()` is the exception: `lib/test/pwent.c` has been checking six of its vectors against the
host's DES all along, and the hash in `/etc/passwd` is one more of the same —
`crypt("root", ".8") == ".8Y/JOGhfuk1I"`. **The plaintext is `root`**, and it is here and in
`kernel/test/login.ini` rather than on the image. If libc's `crypt` ever drifts, `pwent` fails
before `login` does.

The positive answers for `ttyname`/`ttyslot`/`getlogin` moved out of `pwent` and into
[lib/test/ttyt](../../lib/test/ttyt.c) when `/etc/ttys` landed: `pwent` adjudicates both worlds
against one `.expected` and the two no longer agree.

## Three files it wants and this image has not got

Each fails cleanly and each is left in the program rather than cut out, because each comes back
with a task of its own — two of them now, the third having been answered:

* **`/usr/adm/wtmp`** — the permanent login history. `init`'s `rmut()` writes it too, and both
  treat its absence as benign. It is deliberately **not** on the image: nothing reads it, `last`
  is in no task, and a file appended to on every getty respawn with no reader is not worth
  carrying. It belongs with the accounting commands, task C8.
* **`/usr/spool/mail`** — **no longer one of them.** Task C28 put `mail(1)` on the image and
  `../../scripts/root.manifest` the directory with it, so this probe finds something and `You have mail.`
  prints for the first time. It is the only line of `login` whose reader arrived after it did.
* **`/usr/bin`** — named by the PATH `login` exports, which is v7's string kept verbatim. The
  shell's own default already reaches `/bin`.

## What it cost

**6,898 words** of the 28,672 — stdio, `crypt`, the `getpw` family and `getpass`, which is what
makes it the second largest program on the image after `/bin/sh`.
