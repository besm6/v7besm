# mkdir — Unix v7 `mkdir(1)` for the BESM-6, and the first setuid program on the image

`/bin/mkdir`, staged as **`build/rootfs/bin/mkdir`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt). Like [`cmd/pwd/`](../pwd/) and its neighbours this is a
`cmd/` subdirectory that is **not a host tool**. 4,038 words of the 28,672 — nearly all of it
stdio.

The C11 pass is unremarkable and [`cmd/init/README.md`](../init/README.md) is the worked
example of it. What this port is actually about is the **set-user-id bit**, which this program
and [`rmdir`](../rmdir/README.md) are the first two users of on this system. That story is
here; the rmdir README defers to it.

## Why it needs to be setuid, call by call

There is **no `mkdir(2)`** — v7 had none, and neither does this kernel. A directory is built by
hand out of three calls, and every one of them is refused to anyone but the super-user:

| call | where it is gated |
|---|---|
| `mknod(d, 040777, 0)` | `if (suser())` in `mknod()`, [`kernel/sys2.c`](../../kernel/sys2.c) |
| `link(d, "d/.")` | the `IFDIR && !suser()` arm of `link()`, same file |
| `link(pname, "d/..")` | likewise |

`suser()` ([`kernel/fio.c`](../../kernel/fio.c)) is `u.u_uid == 0` and sets `EPERM` otherwise,
so an ordinary user cannot make a directory at all. v7's answer was to make `/bin/mkdir` setuid
root, and it is the right answer here too, because **the program does its own permission
check**: `access(pname, W_OK)` on the parent, before anything else, and `access(2)` asks about
the **real** uid. Root is borrowed for the three calls above and for nothing else — and the
finished directory is immediately `chown`ed to `getuid()`/`getgid()`, the real ids, so nothing
of the borrowed identity is left behind on the disk.

## Where the bit lives, and why it is not in the build tree

**`build/rootfs/` carries no modes.** It is an ordinary staging directory and
`build/rootfs/bin/mkdir` is an ordinary 0755 build artifact; the bit exists in exactly one
place, the stanza in [`root.manifest`](../../root.manifest):

```
file /bin/mkdir         # cmd/mkdir -- SETUID ROOT: mknod(2) and link(2) on a directory
source ../../rootfs/bin/mkdir
mode 04755
```

That works because `b6fsutil` was already able to carry it and nobody had asked: `manifest.cpp`
parses the number as an octal literal, and `add_file()` writes `IFREG | (mode & 07777)` into the
inode — `07777` includes `ISUID`'s `04000`. Nothing masks it off afterwards, and `b6fsutil -c`
does not object. **No source change was needed anywhere to make this work**, which is precisely
why it had to be asserted rather than assumed.

## Asserting it, which is most of the task

The trap is that **the setuid path is unreachable from any shell on this machine.** `getxfile()`
([`kernel/sys1.c`](../../kernel/sys1.c)) reads:

```c
if (ip->i_mode & ISUID)
    if (u.u_uid != 0) {
        u.u_uid = ip->i_uid;
        u.u_procp->p_uid = ip->i_uid;
    }
```

— and every shell here is root's, since `init` execs `/bin/sh` directly with no `getty` and no
`login`. So typing `mkdir` at the console prompt proves that mkdir *works* and proves exactly
nothing about the bit: the branch is not taken. Three tests cover it, and they are three
different questions:

* **`rootimg_setuid`** (a ctest in [`kernel/test/CMakeLists.txt`](../../kernel/test/CMakeLists.txt))
  reads `4755` back off `root.img` with `b6fsutil -v -v`. Host-side, costs milliseconds, and
  runs whether or not SIMH is installed. It answers *did the bit survive being written*.
* **[`lib/test/suidt.c`](../../lib/test/suidt.c)** is the only thing on the image that makes a
  non-root process: it drops to uid 7 (`guest`, from `/etc/passwd`) and execs `/bin/mkdir`. It
  answers *did exec honour the bit* — and it does so with **one call proving both halves**: the
  directory existing proves the effective uid became 0, and its being owned by uid 7 proves the
  real uid stayed 7, because `maknode()` gives a new inode `u.u_uid` (the *effective* one, so
  the directory is born root's) and `mkdir` then chowns it to `getuid()` (the *real* one,
  `lib/libc/sys/getuid.S`). A negative control in the same program requires `mknod()` and
  `unlink()`-of-a-directory to come back `EPERM` for the same uid, so that "the directory
  appeared" cannot be satisfied by a kernel that lets anybody call `mknod`.
* **[`kernel/test/console`](../../kernel/test/console.ini)** and
  **[`kernel/test/session`](../../kernel/test/session.sh)** run both programs as root — the
  *non*-setuid path — and the second leaves a live directory behind for the host-side five-pass
  fsck to reconcile: `/tmp` at nlink 3, `d` at nlink 2, with `.` and `..` pointing where they
  should.

Both `suidt` and `rootimg_setuid` were checked by removing the bit from the manifest and
watching them fail; the first fails with `mkdir: cannot make directory /tmp/suidt.d`, which is
the `EPERM` from `mknod` arriving where it should.

## The two changes beyond the C11 pass

* **`dname[strlen(dname)] = '\0';` is a no-op** — it overwrites the terminator with itself. The
  intent was `- 1`, stripping the trailing dot of `"d/.."` back to `"d/."` so that the `.` link
  made a moment earlier is the one unlinked. As v7 wrote it the cleanup unlinks `"d/.."`, a name
  that by construction does not exist — its link is the one that just failed — so `d` is left an
  allocated directory inode with nlink 1, no entry in any parent, and a `.` pointing at itself.
  That is exactly what `b6fsutil -c`'s third pass reports. **Nothing tests it**: reaching it
  needs `link(2)` to fail, which on this one-filesystem machine means a parent with no room.

* **Neither buffer was bounded.** v7 copies an `argv` string into `char[128]` with `strcpy()`
  and then `strcat()`s onto it. On this machine that walks off the frame, and the 4,096 words of
  stack at `070000` are the one ceiling **nothing checks** (`cmd/README.md` §6). Harmless when
  nothing could reach `mkdir` but the person typing it; not harmless in a program that runs with
  an effective uid of 0. One length test covers both buffers.

## What did *not* need changing, and was checked

No `long`, no `%D`, no `DIRSIZ` assumption, and — the one that would have mattered — **no
relational operator between two `char *`** ([`cmd/ls/README.md`](../ls/README.md)): every `<` in
the file is against a small integer constant. The file-scope helper is renamed `makedir()`, on
the precedent of `ls`'s `readdir`→`listdir`: `<sys/stat.h>` here already declares
`mknod`/`chmod`/`stat` and is one line away from declaring `mkdir`.

The mode really is 0777 rather than 0777-less-a-umask, because `CMASK` is 0 in
[`sys/param.h`](../../include/sys/param.h) and nothing sets another — `maknode()` applies
`~u.u_cmask` all the same. `suidt` asserts the mode, so giving the shell a `umask` builtin will
change that expectation; `mkdir.1` says so too.
