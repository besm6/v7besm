# rmdir — Unix v7 `rmdir(1)` for the BESM-6

`/bin/rmdir`, staged as **`build/rootfs/bin/rmdir`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt); 4,190 words of the 28,672. It is the other half of
[`cmd/mkdir`](../mkdir/), and **the setuid account is there** — why the bit is needed, where it
lives, and how it is asserted. Everything in that file applies here with `unlink()` in place of
`mknod()`: `unlink` of a directory is gated on `suser()` ([`kernel/sys4.c`](../../kernel/sys4.c)),
`rmdir` unlinks three of them, and its own `access(name/.., W_OK)` check — against the **real**
uid — is what decides whether the caller was entitled to any of it.

Three things are this program's own.

## The `np` bug, which corrupts

```c
if ((np = rindex(name, '/')) == NULL)
    np = name;
```

`rindex` returns a pointer **at** the slash, and v7 has no `else np++`. So the guard two dozen
lines further down —

```c
if (!strcmp(np, ".") || !strcmp(np, ".."))
```

— compares `"/."` against `"."` and never fires for a qualified name. Trace `rmdir foo/.` with
the bug: it runs the whole unlink sequence against `"foo/./.."`, `"foo/./."` and `"foo/."`,
which takes out the parent's entry for `foo` and `foo`'s own `.`, decrementing two link counts
that nothing puts back; then the third unlink fails, it prints `not removed`, and `foo` is left
linked into its parent with a link count that is now wrong. `b6fsutil -c` flags it, which is how
[`kernel/test/session`](../../kernel/test/session.sh) guards the fix: that script does
`rmdir /tmp/d/.`, requires the refusal in its transcript, and then fscks the disk on the host.

`rindex` became `strrchr()` in the same edit — no header in this tree declares `rindex`
([`lib/libc/gen/index.c`](../../lib/libc/gen/index.c) says why: it is not ANSI and v7 has no
`<strings.h>`), `<string.h>` declares `strrchr`, and
[`lib/libc/gen/ttyslot.c`](../../lib/libc/gen/ttyslot.c) already uses it for this same
last-component job.

The other change beyond the C11 pass is the length bound on `strcpy(name, d)`, for the reason
[`cmd/mkdir/README.md`](../mkdir/README.md) gives.

## `stat("")`, which is not a typo

```c
if (stat("", &cst) < 0) { ... }
if (st.st_ino == cst.st_ino && st.st_dev == cst.st_dev) { ... }
```

That is how the program refuses to remove the directory it is standing in, and it works because
of a property of this kernel rather than of libc: `namei()` starts at `u.u_cdir` and faults an
empty path only when its flag says create-or-delete —

```c
if (c == '\0' && flag != 0)
    u.u_error = ENOENT;          /* kernel/nami.c */
```

— and `stat` passes 0. So an empty path names the current directory, exactly as v7 intended. If
that rule is ever tightened, this is the caller that breaks, and the symptom will be
`rmdir: cannot stat ""` followed by `exit(1)` in the middle of an argument list.

## The emptiness test, and the two arguments it no longer needs

The loop is [`opendir(3)`](../../lib/libc/man/directory.3.umm) since task C24 — `opendir`,
`readdir` until it returns NULL, `closedir`, and everything that is not `.` or `..` means *not
empty*. That is four lines and it needs no commentary, which is the point of the task.

What it replaced was five lines that needed two paragraphs of defence, because both looked
exactly like bugs the neighbouring ports *did* have to fix:

* **`read(fd, &dir, sizeof dir) == sizeof dir`** was safe as v7 wrote it, unlike the `<` form
  [`cmd/pwd`](../pwd/README.md) had to repair: a failed read's `-1` promotes to an unsigned that
  cannot *equal* 24 either, so the loop still ended.
* **`strcmp(dir.d_name, ".")`** read a bare `char[DIRSIZ]` that the kernel zero-pads but does
  not terminate when a name fills it — the hazard `pwd` and `ls` did have to fix. It could not
  fire here, because `strcmp` stops at the first difference and every name that is not `.` or
  `..` differs within two characters.

Both were true, and both had to be re-derived by every reader. `readdir()` skips the zeroed
`d_ino` a removed entry leaves behind and hands back a terminated name, so neither question
arises. **The cost is 238 words** — 4,221 to 4,459 — which is almost exactly the library's
price for a caller that only walks, this program's own reader having been as small as a reader
gets.

## Documentation

`rmdir` has **no manual page of its own** in v7 and gets none here: it is documented inside
[`cmd/rm/rm.1`](../rm/rm.1.umm), which `cmd/README.md` records as a deliberate decision. The `Rmdir`
half of that page was corrected here; task C1b corrected the `rm` half, so the page is now
whole. It records there that `rm -r` removes an emptied directory by execing **this** program,
which is where its privilege for that comes from — `rm` itself is not setuid and does not need
to be.
