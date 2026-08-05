# mv — Unix v7 `mv(1)` for the BESM-6, and the third setuid program on the image

`/bin/mv`, staged as **`build/rootfs/bin/mv`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt). Like [`cmd/mkdir/`](../mkdir/) and its neighbours this is a
`cmd/` subdirectory that is **not a host tool**. 5,236 words of the 28,672 — the largest of
task C1b's four, and the only one with any real logic.

The C11 pass is unremarkable and [`cmd/init/README.md`](../init/README.md) is the worked
example of it. This port is about three things: **who is allowed to rename a directory**,
**the four calls that do it**, and **an upstream `strcat` into a buffer nobody had
initialized**. `cmd/cp`, `cmd/ln` and `cmd/rm` taught less and are documented in their own
source headers; this file is the one prose account task C1b earns.

## Why it is setuid, and why only half of it

`mv` has three forms, and only the third needs anything:

```
mv f1 f2            rename a file           -- link(2) + unlink(2) on a FILE
mv f1 ... fn d      move files into d       -- likewise
mv d1 d2            rename a DIRECTORY      -- and this is the one
```

There is no `rename(2)` on this system, as there is no `mkdir(2)` or `rmdir(2)`. A directory
is renamed by hand out of `link` and `unlink`, and **both refuse a directory to anyone but the
super-user**:

| call | where it is gated |
|---|---|
| `link(source, target)` | `if ((ip->i_mode & IFMT) == IFDIR && !suser())` in `link()`, [`kernel/sys2.c`](../../kernel/sys2.c) |
| `unlink(source)`, `unlink("target/..")` | the same arm of `unlink()`, [`kernel/sys4.c`](../../kernel/sys4.c) |

So `/bin/mv` is `mode 04755` in [`root.manifest`](../../root.manifest), beside `/bin/mkdir` and
`/bin/rmdir`. [`cmd/mkdir/README.md`](../mkdir/README.md) is the general account of what that
bit costs and how `b6fsutil` carries it; none of it is repeated here.

**What is peculiar to `mv` is that the privilege is dropped for everything else**, and that it
is one line of v7's own placement that does it:

```c
if ((s1.st_mode & S_IFMT) == S_IFDIR) {
    if (argc != 3)
        goto usage;
    return mvdir(argv[1], argv[2]);   /* returns with the borrowed root still held */
}
setuid(getuid());                     /* every other form runs as the real user */
```

Task C1b read that call as "the v7 way of refusing to be setuid", which is
what it amounts to on a system where `/bin/mv` is 0755 — v7's own arrangement, under which
`mv d1 d2` simply did not work for an ordinary user. Here the bit is granted, so the line
becomes load-bearing rather than decorative: **moving it above the `mvdir` return would break
the directory rename, and moving it below the whole block would hand root to a plain file
move.** It is commented in place for that reason.

And the privilege is genuinely borrowed rather than granted, on exactly `mkdir`'s argument:
`mvdir()` makes its own permission decision first, with three `access(2)` calls — on the
target's parent, on the source's parent, and on the source itself — and `access(2)` asks about
the **real** uid. A user who could not have done the rename unprivileged is refused; root is
used only for the mechanics.

## The four calls, and why this task got its own SIMH test

`mvdir()` has two branches and the difference between them is one comparison, `s1.st_ino !=
s2.st_ino` — the two parents.

**Same parent** is a link and an unlink. `..` already points at the right directory, and
nobody's link count changes.

**Different parents** is four calls, and between the first and the last the filesystem is
inconsistent:

```
link(source, target)                    the directory now has two names
unlink(source)                          ...and one again, in the new parent
unlink("target/..")                     the old parent loses the link it should lose
link(pname(target), "target/..")        the new parent gains it
```

`mv` ignores every signal across that sequence, which is right, and which is also why this
port builds and bounds the `"target/.."` string *before* entering it rather than in the middle
of it, where v7 built it.

**A mistake in those four calls is silent.** Every one of them succeeds; what comes out wrong
is a link count, and a wrong link count reads back perfectly from the running kernel — `ls`
cannot see it, `cd` cannot see it, the directory works. Only a pass over the whole i-list can.
That is the whole reason task C1b got [`kernel/test/files`](../../kernel/test/files.sh) instead
of a few more lines in `kernel/test/session.sh`: the test deliberately leaves a re-parented
directory on the disk, and `run-files.sh` ends with `b6fsutil -c`. What it is judging looks
like this, out of `b6fsutil -v -v` on the image the test wrote:

```
d 777 0/0 144 ino  8 nlink 4  /tmp        2 + one `..' per subdirectory (f and g)
d 777 0/0 144 ino 65 nlink 3  /tmp/f      2 + sub2's, which it GAINED
d 777 0/0  48 ino 70 nlink 2  /tmp/f/sub2
d 777 0/0 168 ino 66 nlink 2  /tmp/g      2 -- it gave sub2 up
```

[`lib/test/suidt.c`](../../lib/test/suidt.c) runs the same two branches again from a process
that has really dropped to uid 7, and asserts both parents' counts there too. Between them:
`files` says the arithmetic is right, `suidt` says the setuid bit is what let it happen.

## The five changes beyond the C11 pass

Stated at length in [`mv.c`](mv.c)'s header comment; in brief, and worst first.

* **`strcat(dst, target)` into an uninitialized automatic.** The first thing v7 does with
  `char dst[MAXN+5]` is `strcat`, not `strcpy`. The append therefore starts wherever `strlen()`
  finds a zero byte in the stack garbage and runs off the end from there — and it does it
  *inside the critical section, with every signal ignored, after `link(source, target)` has
  already succeeded*. A crash there leaves the directory with two names and a link count
  nothing puts back. It is `strcpy` now, bounded, and built before the sequence starts.
* **`int status;` was uninitialized** and is read after the `wait()` loop, which can exit on
  `-1` having stored nothing. A cross-device move whose `wait()` failed would report whatever
  was on the stack. Initialized, the `-1` case reported, and the status taken apart with
  `<sys/wait.h>`'s `WIFEXITED`/`WEXITSTATUS`.
* **`utime()` had no declaration anywhere in `include/`.** It is in
  [`<unistd.h>`](../../include/unistd.h) now, matching the SYNOPSIS
  [`lib/libc/man/utime.2`](../../lib/libc/man/utime.2.umm) already carried — and note it takes a
  two-element `time_t` **vector**, not POSIX's `struct utimbuf`, because that is what the
  kernel `copyin`s. v7's `utime(target, &s1.st_atime)` would have worked here, `st_atime` and
  `st_mtime` being adjacent one-word fields, but a struct's field order is not an interface;
  it is an explicit `time_t tv[2]`.
* **`for (i = 1; i <= NSIG; i++)` is off by one.** `NSIG` is 17 and signal numbers run
  1..`NSIG-1`. Harmless, and wrong.
* **Three more unbounded copies.** v7's one length test checked the target but not the
  component being appended to it, and `dname()` returns a pointer into `argv` where nothing
  bounds a component at `DIRSIZ`. Each is checked against the sum it actually builds. That
  test's own arithmetic changed under it as well — `MAXN-DIRSIZ-2` is 80 here and not v7's 84,
  because `DIRSIZ` is 18 — correctly, and only because of the `<sys/dir.h>` include.

## What did *not* need changing, and was checked

No `long`, no `%D`, no `struct direct` — `mv` never reads a directory.

Two things that look like bugs and are not, both left alone with a comment:

* **`pname()` returns a static buffer**, and several sites call it twice in one expression.
  Every one of them is sequenced, or wants the same value twice, so no site holds two live
  results at once. It works; it is fragile; do not extend it.
* **The cross-device path is unreachable on this machine.** `move()` execs `/bin/cp` when
  `link()` fails, which for a rename means `EXDEV` — and there is one filesystem here, one
  EC-5052 being the whole store and swap living on the drums. Task C1b sequenced `cp` onto the
  image before `mv` all the same, because the dependency is in the source and `mv` would fail
  outright without it the day there is a second filesystem to mount. Nothing exercises it.

`mv.1.umm` is the v7 manual page, corrected in place on the
[`lib/libc/man/`](../../lib/libc/man/) precedent: the directory-rename form added to the
SYNOPSIS it was missing from, a new section for what that form costs, a DIAGNOSTICS section
the page never had, and `Note:` paragraphs on the setuid bit and on the `/bin/cp` path that
cannot be reached. Nothing installs it; read it with `nroff -man`.
