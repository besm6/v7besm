# cat — Unix v7 `cat(1)` for the BESM-6

`/bin/cat`, staged as **`build/rootfs/bin/cat`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt). Like [`cmd/init/`](../init/) and [`cmd/sh/`](../sh/), this
is a `cmd/` subdirectory that is **not a host tool**. 4,781 words of the 28,672.

It is also the first command `/etc/rc` runs — `cat /etc/motd >/dev/console` is the whole of the
boot script — so it is the one of the four that the system exercises without being asked.

Almost all of the port is the mechanical C11 pass every v7 source needs (`b6parse` has no
implicit `int`, no K&R parameter lists and no untyped `register c;`). Two things are worth
knowing:

* **The uninitialized `dev` is a real v7 bug, and this machine makes it the common case.** v7
  wrote `int dev, ino = -1;` and filled `dev` in only when the standard output is neither a
  character nor a block device — so on a terminal it is read having never been written. On a
  PDP-11 that was a junk stack word; here it is a junk frame slot, equally undefined, and
  *equally reachable*: `/etc/init` hands the shell three descriptors on `/dev/console`, so
  "stdout is a character device" is the normal state of this system rather than an edge case.
  It happened to be harmless because `ino` stayed `-1` and `&&` short-circuits. Both are
  initialized to `-1` now — that is `NODEV` ([`sys/param.h`](../../include/sys/param.h)), so no
  real `st_dev` can equal it and the "input is output" refusal keeps behaving as v7 intended.

* **The `-`-argument test was parenthesized, not rearranged.** In
  `fflg || ((*++argv)[0] == '-' && (*argv)[1] == '\0')` the short circuit is load-bearing: when
  `fflg` is set, `argc` was forced to 2 and there is no real argument, so `*++argv` must not be
  evaluated and `argv` must not advance. Hoisting the increment out is the obvious cleanup and
  it is wrong.

Nothing here is BESM-6-shaped. `++argv` walks a `char **`, a **thin** word pointer since it
addresses a word-sized object, while `(*argv)[1]` indexes a fat `char *`, and the compiler
handles both ([`doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md) §7).
The one number that changed is the buffer's: `BUFSIZ` is `BSIZE` here, 3,072 bytes, so
`stdbuf` is 512 words rather than the 512 *bytes* of the PDP-11 — the largest object in the
program, and still comfortable.

`cat.1.umm` is the v7 manual page, kept as it was; its "buffered in 512-byte blocks" is 3,072 now.
