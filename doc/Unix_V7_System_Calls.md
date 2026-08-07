# Unix v7 System Calls

## Purpose of this document

What the kernel in this repository offers a user program, call by call: the number, the C
prototype, the routine that implements it, and — briefly — what it does. It is a reference
index, not a reprint of the v7 manual; where a call behaves exactly as v7 documents it, the
entry says so in a line and stops. What the entries do spell out is **where this kernel differs
from stock v7**, because the BESM-6 is a 48-bit word machine and several calls had to change
shape to fit it ([§4](#4-where-this-kernel-differs-from-v7)).

### Where the truth lives

Four hand-maintained copies of the same list, none of which generates any other:

| file | holds | authority |
|---|---|---|
| [kernel/sysent.c](../kernel/sysent.c) | `sysent[64]` — arity and handler | **authoritative**, and POSITIONAL: a call's number is its row index |
| [include/sys/syscall.h](../include/sys/syscall.h) | `SYS_*` — a name per row | names rows; cannot drive them |
| [lib/libc/sys/syscalls.tbl](../lib/libc/sys/syscalls.tbl) | libc symbol → `SYS_*` macro, and the C prototype | what `sys/mkstub` turns into a stub |
| [cmd/sim/syscall.cpp](../cmd/sim/syscall.cpp) | the same table again, as a C++ enum | `b6sim`'s copy; a host tool, so it cannot include `include/` |

Adding or renumbering a call means editing all four — see [§6](#6-adding-a-call).

**The gaps in `syscall.h` are deliberate.** Only rows this kernel implements get a name, so
that naming one can never be mistaken for implementing it ([§5](#5-rows-that-are-not-system-calls)).

**One call is in three of the four copies and not the fourth.** `sigret` (45) is issued by an
instruction the *kernel* assembled and plants on the user stack — the return address of a signal
handler ([§3](#3-signals)) — so no program ever calls it and `syscalls.tbl` has no leaf for it.

## 1. How a call is made

A system call is the extracode **`$77 N`**. The hardware vectors э77 straight to `0577` — never
through `trap()` — and hands the handler the effective address in `r14`; that address *is* the
call number. `sysgate` in [kernel/besm6.S](../kernel/besm6.S) switches to the kernel stack,
builds the 21-word [`reg.h`](../include/sys/reg.h) frame and calls `syscall()`
([kernel/syscall.c](../kernel/syscall.c)), which:

- **range-checks** the number rather than masking it — a user can index-modify the effective
  address to any 15-bit value, and a mask would fold an out-of-range number onto a real call;
- takes the **arity from `sysent[].sy_narg`**, never from the caller. `r14`'s other ABI role,
  the negative argument count, is deliberately ignored;
- **inverts the argument layout**: the C convention puts argument *k* of *n* at `r15 - (n-k)`
  with the last one in the accumulator, and the handlers read `u.u_arg[]` ascending in prototype
  order;
- **pops the n−1 pushed words** on the caller's behalf — the gate stands in for the called
  function and owes it the callee's cleanup;
- returns the result **in the accumulator**, with **`errno` in `r14`** — zero on success; this
  machine has no carry flag — and, for the calls that have one, a **second result in `r12`**.

A libc stub is therefore bare: no prologue, no stack adjustment, `$77 SYS_x` and a branch to
`cerror` if `r14` came back non-zero. [lib/libc/sys/mkstub](../lib/libc/sys/mkstub) generates
exactly that for every call in `syscalls.tbl`; the rest are hand-written beside it. See
[lib/README.md](../lib/README.md) and [doc/Unix_Context_Switch.md](Unix_Context_Switch.md).

The `sy_nrarg` column of `sysent[]` is a PDP-11 leftover and is unused.

## 2. The calls

Prototypes are the ones in [syscalls.tbl](../lib/libc/sys/syscalls.tbl); where libc's entry
point differs from the kernel's name, both are given. **Second result** marks the calls that
return a value in `r12` as well as the accumulator.

### 2.1 Processes

| № | prototype | handler | what it does |
|---|---|---|---|
| 1 | `void exit(int status)` | `rexit` — [sys1.c](../kernel/sys1.c) | Terminate: close every descriptor, release the image, write the accounting record, become a zombie holding `(status & 0377) << 8`, and wake the parent. Never returns. |
| 2 | `int fork(void)` | `fork` — [sys1.c](../kernel/sys1.c) | Duplicate the process. Reserves swap for a maximum image first and refuses (`EAGAIN`) if there is no proc slot, or if a non-superuser would take the last one or exceed `MAXUPRC`. **Second result**: 1 in the child, 0 in the parent — see [§4](#4-where-this-kernel-differs-from-v7). |
| 7 | `int wait(int *status)` | `wait` — [sys1.c](../kernel/sys1.c) | Reap a terminated child, charging its user and system times to the parent; also reports a stopped traced child as `(signal << 8) \| 0177`. Sleeps if there are children but none ready, `ECHILD` if there are none at all. **Second result**: the exit status, which libc's `wait.S` stores through the caller's pointer. |
| 11 | `int exec(char *path, char **argv)` | `exec` → `exece` — [sys1.c](../kernel/sys1.c) | Replace the image. `exec` is `exece` with a null environment. |
| 59 | `int execve(char *path, char **argv, char **envp)` | `exece` — [sys1.c](../kernel/sys1.c) | Stage the argument and environment strings in swap (`NCARGS` bytes maximum), read the `0407`/`0410` header, size and clear the new image, honour set-UID/set-GID unless traced, and lay the argument block at the fixed stack base. See [§4](#4-where-this-kernel-differs-from-v7). |
| 17 | `int _break(char *addr)` | `sbreak` — [sys1.c](../kernel/sys1.c) | Set the program break. Takes a virtual **word** address, not a byte count; libc's `sbrk()` does the conversion. Grows or shrinks the data segment, shuffling the stack pages to follow. |
| 20 | `int getpid(void)` | `getpid` — [sys4.c](../kernel/sys4.c) | Process id. **Second result**: the parent's. |
| 23 | `int setuid(int uid)` | `setuid` — [sys4.c](../kernel/sys4.c) | Set real and effective user id; permitted if `uid` is already the real one, or to the superuser. |
| 24 | `int getuid(void)` | `getuid` — [sys4.c](../kernel/sys4.c) | Real user id. **Second result**: the effective one. |
| 46 | `int setgid(int gid)` | `setgid` — [sys4.c](../kernel/sys4.c) | Set real and effective group id, under the same rule as `setuid`. |
| 47 | `int getgid(void)` | `getgid` — [sys4.c](../kernel/sys4.c) | Real group id. **Second result**: the effective one. |
| 26 | `int ptrace(int req, int pid, int *addr, int data)` | `ptrace` — [sig.c](../kernel/sig.c) | Process tracing. Request ≤ 0 marks *this* process traced; otherwise the request is handed through the single global `ipc` slot to a stopped child, which executes it in `procxmt()` and hands the answer back. `ESRCH` if no such stopped child, `EIO` if the child rejects the request. **`addr` is a word address, and what it counts differs per request** — §4. |
| 27 | `int alarm(int sec)` | `alarm` — [sys4.c](../kernel/sys4.c) | Arm `SIGALRM` for `sec` seconds; returns what was left of any previous alarm. |
| 29 | `int pause(void)` | `pause` — [sys4.c](../kernel/sys4.c) | Sleep until a signal arrives. Nothing ever wakes this channel, so the only way out is the signal. |
| 34 | `int nice(int incr)` | `nice` — [sys4.c](../kernel/sys4.c) | Add to the scheduling `p_nice`, clamped to `0 … 2*NZERO-1`. A negative increment is ignored for a non-superuser. |
| 37 | `int kill(int pid, int sig)` | `kill` — [sys4.c](../kernel/sys4.c) | Send a signal: to one process, to the caller's process group (`pid == 0`), or — for the superuser — to everything but the first two processes (`pid == -1`). `ESRCH` if nothing matched. |
| 43 | `int times(struct tms *buf)` | `times` — [sys4.c](../kernel/sys4.c) | Copy out the four accumulated times (user, system, children's user, children's system) from the u-area. |
| 44 | `int profil(char *buf, int n, int off, int scale)` | `profil` — [sys4.c](../kernel/sys4.c) | **Refused.** A scale of 0 or 1 is v7's "profiling off" and succeeds; any other scale asks to sample and gets `EINVAL`. There is no `addupc()` — see [§4](#4-where-this-kernel-differs-from-v7). |
| 48 | `int (*signal(int sig, int (*f)()))()` | `ssig` — [sys4.c](../kernel/sys4.c) | Set the disposition of a signal and return the previous one, clearing any pending instance. `EINVAL` for signal 0, out-of-range signals, and `SIGKILL`. |
| 51 | `int acct(char *path)` | `sysacct` — [acct.c](../kernel/acct.c) | Superuser: start writing process-accounting records to `path` (a regular file), or stop when `path` is null. `EBUSY` if accounting is already on. |
| 52 | `int phys(int segno, int npages, int physaddr)` | `sysphys` — [machdep.c](../kernel/machdep.c) | Map physical addresses into the user's space. Superuser-checked and then always `EINVAL` — see [§4](#4-where-this-kernel-differs-from-v7). |
| 53 | `int lock(int flag)` | `syslock` — [acct.c](../kernel/acct.c) | Superuser: set or clear `SULOCK`, asking the swapper to keep this process in core. |

### 2.2 Files and I/O

| № | prototype | handler | what it does |
|---|---|---|---|
| 3 | `int read(int fd, char *buf, int n)` | `read` → `rdwr` — [sys2.c](../kernel/sys2.c) | Read from a file, device or pipe at the descriptor's offset; returns the count actually transferred. |
| 4 | `int write(int fd, char *buf, int n)` | `write` → `rdwr` — [sys2.c](../kernel/sys2.c) | The same in the other direction. |
| 5 | `int open(char *path, int mode)` | `open` — [sys2.c](../kernel/sys2.c) | Open an existing file. `mode` is 0/1/2 and is incremented into the `FREAD`/`FWRITE` pair before the permission check. |
| 6 | `int close(int fd)` | `close` — [sys2.c](../kernel/sys2.c) | Release the descriptor, and the file entry with it if this was the last reference. |
| 8 | `int creat(char *path, int mode)` | `creat` — [sys2.c](../kernel/sys2.c) | Create, or truncate an existing file, and open it for writing. The mode is masked by `u_cmask` in `maknode()`; `ISVTX` is never taken from the caller. |
| 19 | `off_t lseek(int fd, off_t off, int whence)` | `seek` — [sys2.c](../kernel/sys2.c) | Reposition: `whence` 0 absolute, 1 relative, 2 from end. `ESPIPE` on a pipe. `off_t` is **one word**, so this is a three-argument call. |
| 18 | `int stat(char *path, struct stat *buf)` | `stat` → `stat1` — [sys3.c](../kernel/sys3.c) | Status of a named file: the in-core inode fields, plus the three dates read back from the disk inode. |
| 28 | `int fstat(int fd, struct stat *buf)` | `fstat` → `stat1` — [sys3.c](../kernel/sys3.c) | The same for an open descriptor; on a pipe the reported size is what is still unread. |
| 41 | `int dup(int fd)`, `int dup2(int fd, int fd2)` | `dup` — [sys3.c](../kernel/sys3.c) | Duplicate a descriptor onto the lowest free one, or — with bit `0100` set in the first argument — onto `fd2`, closing whatever was there. Always a two-argument call; see [§4](#4-where-this-kernel-differs-from-v7). |
| 42 | `int pipe(int fildes[2])` | `pipe` — [pipe.c](../kernel/pipe.c) | Allocate an inode on `pipedev` and two file entries over it. Returns the read descriptor in the accumulator and the write one as the **second result**; libc's `pipe.S` stores both through the caller's array. |
| 54 | `int ioctl(int fd, int req, char *argp)` | `ioctl` — [dev/tty.c](../kernel/dev/tty.c) | Device control. `FIOCLEX`/`FIONCLEX` are handled in the kernel proper; everything else goes to the driver's `d_ioctl`. `ENOTTY` unless the descriptor is a character device. |
| 31 | `int stty(int fd, struct sgttyb *buf)` | `stty` — [dev/tty.c](../kernel/dev/tty.c) | The obsolete terminal-set call, implemented as `ioctl(fd, TIOCSETP, buf)`. |
| 32 | `int gtty(int fd, struct sgttyb *buf)` | `gtty` — [dev/tty.c](../kernel/dev/tty.c) | Likewise `ioctl(fd, TIOCGETP, buf)`. |

### 2.3 Filesystem

| № | prototype | handler | what it does |
|---|---|---|---|
| 9 | `int link(char *target, char *linkname)` | `link` — [sys2.c](../kernel/sys2.c) | Add a directory entry for an existing file. Directories only for the superuser; `EXDEV` across devices, `EEXIST` if the new name is taken. |
| 10 | `int unlink(char *path)` | `unlink` — [sys4.c](../kernel/sys4.c) | Zero the directory entry and drop the link count. Directories only for the superuser; refuses a mount point (`EBUSY`) and the last link of a running shared text (`ETXTBSY`). |
| 12 | `int chdir(char *path)` | `chdir` → `chdirec` — [sys4.c](../kernel/sys4.c) | Change the working directory, after checking it is a directory and searchable. |
| 61 | `int chroot(char *path)` | `chroot` → `chdirec` — [sys4.c](../kernel/sys4.c) | The same for the process's root; superuser only. |
| 14 | `int mknod(char *path, int mode, int dev)` | `mknod` — [sys2.c](../kernel/sys2.c) | Create a special file or directory with the given device number; superuser only. |
| 15 | `int chmod(char *path, int mode)` | `chmod` — [sys4.c](../kernel/sys4.c) | Set the permission bits. Only the superuser can set `ISVTX`; clearing it releases a sticky text image. |
| 16 | `int chown(char *path, int uid, int gid)` | `chown` — [sys4.c](../kernel/sys4.c) | Set owner and group; superuser only. **Three arguments**, unlike the two-argument PDP-11 v6 form. |
| 30 | `int utime(char *path, time_t *times)` | `utime` — [sys4.c](../kernel/sys4.c) | Set the access and modification times from a two-word array; owner or superuser. The change time cannot be set. |
| 33 | `int access(char *path, int mode)` | `saccess` — [sys2.c](../kernel/sys2.c) | Test read/write/execute permission using the **real** uid and gid, which it swaps in for the duration of the call. |
| 60 | `int umask(int mask)` | `umask` — [sys4.c](../kernel/sys4.c) | Set the file-creation mask (`& 0777`) and return the previous one. |
| 21 | `int mount(char *spec, char *dir, int rdonly)` | `smount` — [sys3.c](../kernel/sys3.c) | Mount a block device on a directory. Validates the superblock with `sbcheck()` and refuses a bad one with `EINVAL` rather than letting `getfs()` "repair" it later. |
| 22 | `int umount(char *spec)` | `sumount` — [sys3.c](../kernel/sys3.c) | Unmount: flush, release the sticky texts, and refuse (`EBUSY`) while any inode on the device is still in use. |
| 36 | `int sync(void)` | `sync` — [sys4.c](../kernel/sys4.c) | Write out the superblocks, the inode table and the delayed-write buffers (`update()`). Cannot fail. |

### 2.4 Time

| № | prototype | handler | what it does |
|---|---|---|---|
| 13 | `time_t time(time_t *tloc)` | `gtime` — [sys4.c](../kernel/sys4.c) | Seconds since the epoch, in the accumulator. The gate takes **no** argument; the store through `tloc` is libc's own doing ([time.S](../lib/libc/sys/time.S)). |
| 25 | `int stime(time_t t)` | `stime` — [sys4.c](../kernel/sys4.c) | Set the system time; superuser only. **One argument**: `time_t` is one 48-bit word. |
| 35 | `int ftime(struct timeb *tp)` | `ftime` — [sys4.c](../kernel/sys4.c) | Time with milliseconds — derived from `lbolt` at `HZ` = 250 — plus the compiled-in `TIMEZONE` and `DSTFLAG`. |

### 2.5 Kernel introspection

**Two rows here are not v7's**, and they are the only calls in this table that no PDP-11 Unix
had.

| № | prototype | handler | what it does |
|---|---|---|---|
| 49 | `int kctl(const char *name, int op, void *buf, int len)` | `kctl` — [kctl.c](../kernel/kctl.c) | Read the kernel's table of exported variables. `KCTL_GET` copies the variable's value, `KCTL_STAT` a `struct kctlstat` holding its **word** address and size, `KCTL_LIST` the exported names as `KSYMLEN`-byte records, `KCTL_PSINFO` a `struct psinfo` per `proc[]` slot (see below); `KCTL_SET` is reserved and answers `EINVAL`. At most `len` bytes are copied and the count is returned, `read(2)`-fashion; `len == 0` copies nothing and reports the size *available*. `ENOENT` for an unknown name, `EINVAL` for an unknown op or a name with no NUL in `KSYMLEN` bytes, `EFAULT` for a bad pointer. **Not privileged.** |
| 50 | `int statfs(const char *path, struct statfs *buf)` | `statfs` — [sys3.c](../kernel/sys3.c) | The four counts `df(1M)` prints — `s_fsize`, `s_isize`, `s_tfree`, `s_tinode` — for the filesystem holding `path`, out of the **in-core** superblock. `namei()` crosses a mount, so `statfs("/mnt")` answers for what is mounted there. `ENXIO` if the filesystem is not mounted, `EFAULT` for a bad pointer, and `stat(2)`'s errors from the path walk. **Not privileged.** |

**Why it exists.** Every other Unix finds a kernel variable with `nlist(3)` over the kernel
image. **There is no kernel image on this root filesystem** — [root.manifest](../root.manifest)
names no `/unix`, and `kernel/unix.ini` has the *simulator* load one off the build host — so
`nlist(3)` would have nothing to open and is deliberately absent from this libc
([lib/libc/README.md](../lib/libc/README.md)). RetroBSD met the same wall, its kernel living in
flash, and answered it by carrying a hand-written table inside the kernel and publishing it
through one `sysctl(2)` node. This is the same idea without the framework: no MIB, no name
space, one call, and a table that cannot drift from the image it is part of because every entry
is a link-time relocation of the real declaration.

**What it does not replace.** `KCTL_GET` is enough for a program that reads fixed variables —
`dmesg`, `iostat` and `vmstat` open no device and need no privilege. It is *not* enough for
`pstat`, which resolves `p_textp` into an index in `text[]`: that is arithmetic against a base
address, which is what `KCTL_STAT` and the `kgetsym(3)` shorthand over it return. `pstat` then
reads on through `/dev/kmem`, and through `/dev/mem` for a u-area at `p_addr`, which lies above
`KREACH` and out of `/dev/kmem`'s reach ([kernel/dev/mem.c](../kernel/dev/mem.c)).
[lib/test/memt.c](../lib/test/memt.c) is that ladder's first rung and
[lib/test/kctlt.c](../lib/test/kctlt.c) is this one's.

**`pstat` is the only program left on that ladder.** `ps` climbed it too — the same two
devices, both mode 0640 and root's, for four fields of a u-area — until `KCTL_PSINFO`. That
operation returns a `struct psinfo` per `proc[]` slot: the pid as a join key, `u_utime +
u_stime`, the index of `u_ttyp` in `sc[]`, and `u_comm`. The kernel does the walking, so `ps`
opens no device and is an ordinary user's command; the same reasoning made `df(1M)` one, by way
of `statfs(2)` above. **Neither took a permission bit and neither loosened a device mode** —
`/dev/kmem`, `/dev/mem` and `/dev/rmd0` are exactly as they were, which
[lib/test/unprivt.c](../lib/test/unprivt.c) asserts before it asserts anything else.
`KCTL_PSINFO` is an *operation* and not a table row because a digest computed at the moment of
asking has no address to relocate; it took no system-call number for the reason
[§6](#6-adding-a-system-call) gives.

The interface is [include/sys/kctl.h](../include/sys/kctl.h); the table is
[kernel/kctl.c](../kernel/kctl.c), and every row in it names the program that asked for it.

**`b6sim` answers this call too**, from a kernel it pretends to be running under
([cmd/sim/kernel.h](../cmd/sim/kernel.h)) — the same thirty-three names, the guest's own pid and uid
in `proc[0]`, one `KCTL_PSINFO` record for that slot and empty ones for the rest, zeros where
the simulator has no counterpart, and `/dev/kmem` and `/dev/mem` served from the same block.
`statfs(2)` is the exception it answers with `ENODEV`: every path there is the *host's*, and
the host has no v7 superblock to report in these units. `ENODEV` and not `EPERM`, which would
read as "you are not root" — the one thing that call exists to stop meaning. That is what lets [lib/test/kctlt](../lib/test/kctlt.c) run in
**both worlds against one `.expected`**, which is the only guard there is on the guest struct
layouts b6sim has to respell: `kctlt` computes them from the real headers and a drifted offset
fails under the simulator while passing on the image.

## 3. Signals

`signal` (48) is the only signal *call* a program makes, but three more of the entries above are
signal-shaped: `kill` sends, `alarm` arms `SIGALRM`, and `pause` waits. Delivery is the shared tail
of both extracode doors — `sysret()` in [syscall.c](../kernel/syscall.c) calls `issig()`/`psig()` on
every return to user mode, so a signal raised by a call is delivered before that call returns,
and a signal that interrupts a `sleep()` unwinds through `u.u_qsav` and turns the call into
`EINTR`.

**`sigret` (45) is the fifth, and no program issues it.** `psig()` calls `sendsig()`
([kernel/sendsig.c](../kernel/sendsig.c)), which copies the 21-word trap frame onto the user stack,
plants a `$77 SYS_sigret` word above it — a copy of `sigcode` from
[kernel/besm6.S](../kernel/besm6.S) — and enters the handler with the signal number in the
accumulator and that word's address in r13. The handler's own `13 uj` therefore trips the extracode,
and `sigret()` reloads the frame. It takes no arguments (the frame is at `r15 - (NREGFRAME + 1)`),
it masks SPSW down to `SPSW_USER` so a forged frame buys nothing, and it sets `u.u_justreturn` so
that `syscall()` does not write a result over the three registers it has just restored. Two
consequences for libc: the `signal` stub is a plain generated leaf with no trampoline behind it, and
a handler is entered by the ordinary one-argument calling convention. The full account is
[Unix_Context_Switch.md §10a](Unix_Context_Switch.md#10a-the-signal-frame).

## 4. Where this kernel differs from v7

These are the facts a reader cannot get from a v7 manual page.

- **`seek` and `stime` lost a word each.** On the PDP-11 an `off_t` and a `time_t` were two-word
  `long`s, so `seek` took four arguments and `stime` two. Here each is one 48-bit word
  ([include/sys/types.h](../include/sys/types.h)), so `sysent[]` says 3 and 1 — which is also
  what the handlers' own argument structs say.

- **`break` takes a word address.** On a word-addressed machine the break names a word, so no
  `btow()` is applied to the argument; libc's `sbrk()` converts its byte increment on its own
  side. `ptrword()` masks the value to bits 15–1, so a fat `char *` and a plain word address are
  both accepted (a mid-word pointer floors to its word). The ceiling is not written in
  `sbreak()` but in `estabur()`'s `nt + nd > USTKPAGE * PGSZ` ([kernel/utab.c](../kernel/utab.c)):
  the data segment stops where the stack begins, at `070000`.

- **`ptrace`'s `addr` is a word address, and what it counts depends on the request.** The
  argument is declared `int *` — a *thin* pointer, bits 15–1 — and `ptrace()` hands it to the
  stopped child untouched ([kernel/sig.c](../kernel/sig.c)), so a word address is what the ABI
  promises. `fuword()`/`suword()` mask to bits 15–1, so a fat `char *` is accepted and floors to
  its containing word; there is no way to name a *byte*, and requests 1, 2, 4 and 5 transfer whole
  48-bit words. The units are not the same across requests, and two of them are not v7's:

  | request | `addr` is |
  |---|---|
  | 1, 2 (read I/D), 4, 5 (write I/D) | a word address in the child's address space. There is no I/D separation, so 1 and 2 name the same space, and so do 4 and 5. |
  | 3, 6 (read/write u) | a word **index** into the u-area, `0 … USIZE-1`. v7's was a byte offset. |
  | 7 (continue) | the resume PC, a word address, with `1` reserved for "leave it where it stopped". |
  | 9 (single-step) | nothing: the request is **refused with `EIO`** and `addr` is never read. There is no T-bit on this machine, and stepping with the address-break registers М034/М035 would need an instruction decoder in the kernel — [Besm6_Kernel_Reference.md](Besm6_Kernel_Reference.md), "Known consequences, accepted". The child is left stopped, so request 7 still works. |

  `data` and the returned word are likewise whole words, and **−1 is a legal result**: `r14`
  says whether the call failed, so libc's stub needs none of v7's `errno = 0` preamble. `b6sim`
  refuses `ptrace` with `EPERM`, so nothing about this row is observable except under a boot.

- **`fork` returns "which am I" in `r12`.** Nothing advances the saved PC to skip an instruction
  in the parent: the extracode gate already stored `nextpc` in ERET, and `RET` is a *word*
  address here, so bumping it would step whole instruction words. Instead the second result is
  1 in the child and 0 in the parent, and the first is the *other* process's pid.

- **`exec` builds the argument block at a fixed address.** The BESM-6 user stack grows **up**
  from `USTKPAGE * PGSZ` = `070000`, so `argc` is always at absolute `070000`, the pointer
  vector and the strings follow, and `r15` starts above them — a program's own stack growth can
  never walk back over its arguments, and a `crt0` finds them with no register hand-off. The
  `argv[i]`/`envp[i]` entries are **fat pointers** (marker in bit 48, byte offset in bits 47–45),
  because a plain word address is not a valid `char *` on this machine. The vector strides by one
  *word* because `suword()` takes a word address.

- **`dup` always takes two arguments**, both in the kernel and in `b6sim`, with bit `0100` of the
  first selecting `dup2`. A C `dup(fd)` has only one, so [dup.S](../lib/libc/sys/dup.S) pushes it
  and passes a dummy second argument to balance the gate's pop.

- **`phys` is a stub.** It checks `suser()` and then returns `EINVAL`. The v7 PDP-11 call handed
  a user a physical segment through the segmentation registers; the equivalent here would be a
  raw `РП` entry, and nothing needs one yet.

- **`profil` refuses.** A scale of 0 or 1 is v7's own spelling of "profiling off", which is this
  machine's permanent state, so those succeed; any other scale is a request to sample and gets
  `EINVAL`. There is no `addupc()` and no `u_prof` in the u-area behind it — nothing in userland
  profiles (`monitor`/`mcount`, `cc -p` and `prof(1)` are all absent and are not coming), and
  accepting the call while recording nothing was the worse of the two answers. The full account is
  the `profil(2)` bullet under "Known consequences, accepted" in
  [Besm6_Kernel_Reference.md](Besm6_Kernel_Reference.md).

- **The epoch starts at 0.** The machine has no clock-calendar a program can read, so `time`
  counts from boot until something calls `stime` (`clkstart()` in
  [machdep.c](../kernel/machdep.c)). The interval timer free-runs at `HZ` = 250 and cannot be
  programmed — the kernel can only mask it.

- **`signal` registers a disposition and no more, for now.** There is no `dvect`/`tvect`
  trampoline in libc of the kind the x86 port carried; a kernel-side signal frame comes with the
  rest of the libc work ([lib/README.md](../lib/README.md)).

- **`ioctl` and `stty`/`gtty` reach only what the drivers implement** —
  [kernel/dev/](../kernel/dev/) has the two Consul typewriters (`sc`), `sy`, memory, drum and disk.
  A `TIOC*` command outside the set `ttioccomm()` implements gets `ENOTTY`: a Consul has no line
  speed and no modem control behind the rest of them.

`b6sim` implements the same set at user level, but not identically: `mount`, `umount`, `ptrace`,
`profil`, `acct` and `phys` are refused with `EPERM`, and `ioctl` and `lock` are accepted
no-ops. Two entries need more than a clause. **`signal` runs a guest handler** — on the guest,
at the end of a serviced extracode, which is where the kernel delivers too — so it is no longer
the `SIG_DFL`/`SIG_IGN`-only stub this sentence used to describe. And **`kctl` is answered from
an imitation kernel**: b6sim carries the same thirty-three variables, fills in what it genuinely
knows and zeroes what it does not, answers `KCTL_PSINFO` for its one process, and serves
`/dev/kmem` and `/dev/mem` from the same block — which is what lets `lib/test/kctlt` run in
*both* worlds against one `.expected` rather than only under a boot. `statfs` is the one row
b6sim refuses on purpose, with `ENODEV`. See
[Aout_Simulator.md §7](Aout_Simulator.md#7-system-calls).

## 5. Rows that are not system calls

Eleven rows of `sysent[]` dispatch to one of two stubs in [kernel/trap.c](../kernel/trap.c):
`nullsys()` does nothing and succeeds, `nosys()` sets `EINVAL`.

| rows | v7 name | dispatches to |
|---|---|---|
| 0 | `indir` | `nullsys` — the indirect call, inoperative |
| 38 | `switch` | `nullsys` — inoperative in v7 too |
| 39 | `setpgrp` | `nullsys` — not implemented yet |
| 40 | `tell` | `nosys` — obsolete |
| 55–58, 62, 63 | USG-reserved, `readwrite`, `mpxchan` | `nosys` |

None of them has a `SYS_*` name, on purpose. **Three rows have left this table.** Row 45, v7's
"unused", is `sigret` ([§3](#3-signals)); rows 49 and 50, v7's two "reserved for USG", are
`kctl` and `statfs` ([§2.5](#25-kernel-introspection)) — this port's own calls, which took the
lowest free rows rather than numbers past 63, since going past 63 would have moved `NSYSENT`
and the range check with it. Two more paths reach the same place:

- **An out-of-range number.** `syscall()` dispatches `badsysent` — a private `{0, 0, nosys}` —
  rather than masking the number onto a real row.
- **A neighbouring extracode.** э50–э76 vector to `badext`, whose C side `badextr()` sends
  `SIGILL`. Two things arrive there that do not look like extracodes: э20/э60 and э21/э61 share
  a vector word each, and a user `стоп` is re-dispatched as э63.

## 6. Adding a call

1. **[kernel/sysent.c](../kernel/sysent.c)** — fill the row at the number you want. `sy_narg` is
   the arity of the **C prototype**, and it is the only thing that tells the gate where the
   arguments are: a count that disagrees with the caller reads every argument from the wrong slot
   *and* drifts the user stack by a word per call.
2. **[include/sys/syscall.h](../include/sys/syscall.h)** — add `SYS_name`, spelled as `sysent.c`
   spells it. One number, one name, no aliases.
3. **[lib/libc/sys/syscalls.tbl](../lib/libc/sys/syscalls.tbl)** — add `symbol macro # prototype`
   if the stub is uniform (issue the extracode, return, `cerror` on failure). If it is not — a
   second result, an argument that must survive the trap, a call that does not return — write the
   `.S` leaf by hand beside the table and leave it out.
4. **[cmd/sim/syscall.cpp](../cmd/sim/syscall.cpp)** — add the enum value *and* the arity in
   `syscall_nargs()`, or `b6sim` and the kernel will disagree about the stack.
5. Declare the handler in [include/sys/systm.h](../include/sys/systm.h), and add the source to
   `KSRC` in [kernel/CMakeLists.txt](../kernel/CMakeLists.txt) if it is a new file. A new
   **header** needs a re-configure on top of that (`make clean; make`): the coarse dependency
   list is a `file(GLOB)` and runs at configure time.
6. Give the user-visible prototype a home. `<unistd.h>` is the default; a call with a structure
   or a set of operation codes of its own goes beside them instead, as `stat`, `wait`, `kctl`
   and `statfs` do — [include/unistd.h](../include/unistd.h)'s head comment keeps that list.

**Before doing any of it, ask whether it needs a number at all.** A system-call number is
permanent and externally visible; an *operation* on a call that already exists is neither, and
costs none of steps 1–4. `KCTL_PSINFO` is the worked example: `ps` needed three u-area columns,
and they arrived as a fourth operation on `kctl(2)` — one `#define`, one struct in
`<sys/kctl.h>` and one arm in [kernel/kctl.c](../kernel/kctl.c), with the existing
`lib/test/kctlt` conformance test extending to cover it. `statfs` is the counter-example and
says why the line falls where it does: `kctl`'s first argument is a *symbol name*, read for at
most `KSYMLEN` (12) bytes, so a pathname would fit for `/tmp` and not for `/usr/include/sys` —
the worst behaviour available. **`kctl` answers questions about the kernel; `statfs` answers a
question about a filesystem named by a path.**

`kctl` (49) is the only call this port has added, and its commit is the worked example of all
six steps.

**Two things about a new call fail silently and nothing but a test catches them.** An `sy_narg`
that disagrees with the prototype drifts the user stack by a word per call, and a table the
kernel copies out with the wrong stride comes back the right *length*. `lib/test/kctlt.c`
asserts both — a 200-call loop between two frame locals for the first, and a walk that stats
every name `KCTL_LIST` returned for the second. Both found a real bug when they were written.

## 7. See also

- [Unix_Context_Switch.md](Unix_Context_Switch.md) — the four gates, the `reg.h` frame, and the
  shared `intret` exit that every one of these calls returns through.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the C ABI the syscall ABI is
  built on: arguments in direct order, the last in the accumulator, `r14`/`r13`.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — why `struct stat` is one word
  per field, and what a fat `char *` is.
- [Aout_Simulator.md §7](Aout_Simulator.md#7-system-calls) — the same set as `b6sim` services it.
- [lib/README.md](../lib/README.md) — the libc side: `crt0`, the stubs, `cerror`, and the work
  that is left.
- [kernel/README.md](../kernel/README.md) — how the kernel works, and
  [Besm6_Kernel_Reference.md](Besm6_Kernel_Reference.md) for the rules behind it.
