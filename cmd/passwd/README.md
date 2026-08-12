# `passwd`, `su`, `newgrp` — the first setuid bit that is not about a directory

Task C6e. The C11 pass is each source's own header; this is what the three of them taught
together, and it belongs in one place because the thing they have in common is the whole subject.

## Three years of setuid, and it had never been about identity

[../README.md](../README.md) §8 has said since task C1 that setuid works and is asserted. But the
three programs carrying the bit were `mkdir`, `mv` and `rmdir`, and all three want it for the same
narrow reason: **there is no `mkdir(2)`, `rmdir(2)` or `rename(2)` on this system**, a directory is
built and taken apart by hand, and `mknod()` and the directory arms of `link()`/`unlink()` are
`suser()`-gated. Root is borrowed for one syscall and given straight back.

These three borrow it for something else. What `su` changes is **who the caller is**, and it does
not give it back — it cannot, because `setuid(2)` here moves the real id too
([kernel/sys4.c](../../kernel/sys4.c)) and there is no saved id to climb back through. That is not
a limitation, it is the property the program exists to have.

**And they are what finally makes `getxfile()`'s ISUID branch ordinary.** §8's trap is that the
branch is taken only `if (u.u_uid != 0)`, and until kernel task 29b every shell on this image was
root's, so the branch was unreachable from a prompt and [../../lib/test/suidt.c](../../lib/test/suidt.c)
had to drop to uid 7 on purpose to reach it at all — an experiment, run by a program written for
the purpose. In `kernel/test/accounts` a guest types `su` and the branch fires by itself. §8's
advice still stands for anything that wants to *test* the bit; what changed is that the bit is now
something a user meets rather than something a test arranges.

## `login` is not setuid and these are, and the pair is the design

[../login/README.md](../login/README.md) states it from the other end: `login`'s privilege is
**inherited** — `getty` is `init`'s child and already root — rather than borrowed, so v7 ships
`/bin/login` 0755 and so does [../../scripts/root.manifest](../../scripts/root.manifest). v7's `login.1` claims the
command "may be used at any time to change from one user to another"; that use needs the bit, and
the command for it is `su`. The note in `cmd/login/CMakeLists.txt` named this task in advance.

So the door is opened in exactly one place, by a 52-line program whose whole content is a
`crypt()` comparison, rather than by making the 6,898-word `login` setuid and reachable from a
shell.

## What each one actually needs the bit for, which is three different things

| | the gate | why nothing smaller would do |
|---|---|---|
| `su` | `setuid()`/`setgid()` to another account | `setuid(2)` admits `u_ruid == uid` or `suser()`; changing *to* someone else is the second case by definition. |
| `newgrp` | `setgid()` to a group the caller is not in | `setgid(2)` admits `u_rgid == gid` or `suser()`; a group you are already in needs no command. |
| `passwd` | write permission on `/etc/passwd` | the file is 0644 and root's. This is the narrowest of the three: it wants a *file*, not an identity. |

`getuid()` returning the **real** uid is what makes v7's code correct in all three. By the time
`main()` runs the effective uid is already 0, so `su`'s `getuid() == 0` (skip the password) and
`passwd`'s `u != 0 && u != pwd->pw_uid` (permission denied) are asking about the person who typed
the command, which is the only question worth asking.

## `newgrp` execs a shell on every path, including every refusal

`newgrp nosuchgroup` prints its complaint and then hands back a **shell**, with the group
unchanged. So does `Sorry`. So does the usage message. It looks like a bug and is not: the program
was `exec`ed by a shell that is still waiting for it, and on a login shell an exit would end the
session. There is nothing an exit status could say that the next prompt does not.

Two things follow, and both are in `newgrp.1` now because they were in neither the page nor the
code:

* **`newgrp` is not a shell builtin here.** v7's page says "Newgrp is known to the shell, which
  executes it directly without a fork" — [../sh/](../sh/) has no such builtin, so `sh` forks like
  it does for anything else, and the new group lasts only as long as *that* shell. The login shell
  underneath is untouched.
* **`other` is a name and not a policy.** v7 exempts the group literally called `other` from the
  membership test. [../../etc/group](../../etc/group) has it as gid 1, and `guest` logs in as gid
  3 — so the page's "when most users log in, they are members of the group named `other'" is not
  true of this image, and the exemption is the only reason `newgrp other` works at all.

## `passwd` rewrites the file by copying it twice, and leaks its own lock

There is no `rename(2)` in this kernel, so the sequence is: `creat("/etc/ptmp", 0600)`, copy the
whole password file through `getpwent()` with the matching hash replaced, then
`creat("/etc/passwd", 0644)` and copy back. `/etc/passwd` keeps its i-number, which is the one
thing the arrangement buys; the window between the two copies is real and is v7's.

**`/etc/ptmp` is also the lock, and every error path after the `creat()` jumps to `bex`, which does
not unlink it.** An interrupted `passwd` therefore leaves a file that makes every later run say
`Temporary file busy` — forever, until somebody removes it by hand. It is left alone deliberately:
the fix is to unlink a file this process may not have created, which is worse than the leak. The
manual page says so now, since nothing else can.

**The one fix is the exit status.** v7 fell into `bex: exit(1)` on *every* path including the one
that had just rewritten the file, so no script could tell a changed password from a refused one.
Success is 0.

## The oracle is a round trip, because the hash cannot be a literal

The salt is `time() + getpid()`, so the thirteen characters `crypt()` produces are different on
every run and no `.expected` can hold them. [../../kernel/test/accounts](../../kernel/test/accounts)
asserts the thing that does not vary:

* `guest` ships with an **empty** password field, so the first login has no `Password:` prompt at
  all — the dialogue's stage 3 fires only if none appears;
* `passwd` gives the account one;
* the session logs out, `init`'s `multiple()` respawns the getty;
* the same name logs in again and **is asked for a password**, which it was not four stages
  earlier, and the one `guest` typed satisfies `crypt(3)` on the way back in.

A prompt that did not exist before is a stronger statement than any hash comparison, and it is the
only one available. `run-accounts.sh` adds what the console cannot see: the field is thirteen
characters where it was empty, **every other line of `/etc/passwd` is byte-for-byte what was
staged** (`passwd` reprints the whole file field by field through `getpwent()`, and a field it
mangled in passing would be invisible to the dialogue), and `/etc/ptmp` is gone.

`newgrp`'s effect is invisible from inside the guest for task C1's reason — `ls -l` prints the
owner and what changed is the group — so the assertion is two files created seconds apart in one
directory by one user, `/tmp/before` at gid 3 and `/tmp/after` at gid 1, read back off the disk by
`b6fsutil -v -v`.

## One thing the test found that is the shell's, not this task's

**`exit` does not leave an interactive shell.** [../sh/error.c](../sh/error.c)'s `exitsh()` is v7's
verbatim:

```c
if ((flags & (forked | errflg | ttyflg)) != ttyflg) done();
else { clearup(); longjmp(errshell, 1); }
```

With `ttyflg` set, not forked and no error, `exit` longjmps back to the read loop and the shell
carries on. Every dialogue in `kernel/test/` types `^D` for this reason, and `accounts.ini` had to
be rewritten to do the same after `exit` produced a stage that looked exactly like a hang: the
echo, and then another prompt. It is recorded here rather than in `cmd/sh/README.md` because the
place it costs an afternoon is a new `.ini` file.

## Sizes

|  | const | text | data | bss | total |
|---|---|---|---|---|---|
| `passwd` | 97 | 4,762 | 419 | 1,401 | **6,679** |
| `newgrp` | 92 | 4,346 | 508 | 1,496 | **6,442** |
| `su` | 90 | 4,141 | 338 | 1,305 | **5,874** |

Of the 28,672 available. All three are `crypt` and the `getpw`/`getgr` families and stdio, which is
why they sit within a few hundred words of each other and of `login`'s 6,898 — the programs
themselves are 52, 57 and 172 lines.
