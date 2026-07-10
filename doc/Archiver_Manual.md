# BESM-6 Unix Archiver Manual

This manual describes `b6ar`, the BESM-6 archiver in `cmd/ar/`. An archiver packs many files
into a single **archive** (`.a`) file. Its main use is bundling the object files produced by the
assembler (`b6as`) into a **link library** that the linker (`b6ld`) can search: instead of naming
a dozen `.o` files on the link line, you name one `.a` and the linker pulls out only the members
it needs. `b6ar` also serves as a general file bundler — it can add, replace, delete, list,
print, extract, and reorder the members of any archive. This document is both a tutorial (start
at [§1](#1-overview)) and a complete reference for the command line and the on-disk `.a` format.
It does **not** re-document the object format the members contain, the assembler, or the linker;
for those, see:

- [Linker_Manual.md](Linker_Manual.md) — `b6ld`, which consumes the `.a` libraries `b6ar` builds
  ([§7](Linker_Manual.md#7-archives-and-libraries) covers how archives are searched, and
  [§8.7](Linker_Manual.md#87-archive-and-ranlib-structures) the archive/ranlib structures).
- [Assembler_Manual.md](Assembler_Manual.md) — `b6as`, which produces the object files that make
  up a typical library.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — how a 48-bit word is laid out,
  the basis for the word-alignment rules below.

Everything below is derived from the archiver sources themselves: `main.c` (command-line front
end), `ar.c` (argument parsing and command dispatch), `command.c` (the seven command handlers),
`archive.c` (the member-streaming engine and insertion positioning), `list.c` (the verbose
listing), `util.c` (diagnostics, the exit path, small helpers), and the shared headers
`intern.h`/`archive.h`. The on-disk archive format is defined in
[`cross/besm6/ar.h`](../cross/besm6/ar.h); its byte-level serialization
(`getarhdr`/`putarhdr`/`getint`/`putint`) lives in [`cmd/libaout/`](../cmd/libaout/).

> **Octal convention.** Octal is the native notation for BESM-6, and this manual writes the
> magic number and other machine constants in octal (a leading `0`, as in the C sources:
> `ARMAG` is `0177545`). Decimal is used only for human-scale counts (byte lengths of on-disk
> structures, such as the member header).

> **Word addressing.** The BESM-6 is a **48-bit, word-addressed** machine: one word is **6
> bytes**, the constant `W` that pervades the toolchain. `b6ar` treats member data as opaque
> bytes, but it pads every member up to a whole word so that the linker — which *is*
> word-oriented — can walk the archive. Sizes and offsets in the format section below are
> therefore always multiples of 6.

---

## 1. Overview

An **archive** is several files glued together into one, each stored as a **member**. On disk the
layout is a one-word magic number followed by a run of members, each a fixed header describing the
file (name, date, owner, permissions, size) immediately followed by the file's raw bytes:

```
  [ magic word ARMAG ]            one BESM-6 word (6 bytes), marks the file as an archive
  [ member 1 header ][ data ]     header = struct ar_hdr (variable size); data = the file's bytes
  [ member 2 header ][ data ]
  ...
```

Each member's data is zero-padded up to a whole word so that the next header always begins on a
word boundary (see [§6](#6-the-archive-file-format)). The magic number is `ARMAG` (`0177545`); a
file whose first word is anything else is "not in archive format" and is rejected.

Members are stored under their **basename** only: `b6ar r lib.a src/util.o` stores the member as
`util.o`, and later commands match it by that short name. A member name may be up to `ARMAXNAME`
(255) bytes long; a longer basename is rejected.

`b6ar` builds no symbol index of its own — it is a pure container. The optional `__.SYMDEF`
table of contents that lets the linker find a symbol without scanning every member is added by
the separate `b6ranlib` tool; see [§7](#7-symbol-tables-and-b6ranlib).

---

## 2. Invoking the archiver

```
ar [mrxtdpq][uvnbail] archive file ...
```

The first argument is a bunched string of letters (no leading dash): exactly one **command
letter** and any number of **option letters**, in any order. The second argument is the archive
path. The remaining arguments are the member names the command operates on.

| Argument | Meaning |
|----------|---------|
| `argv[1]` | the flag string — one command letter from `mrxtdpq`, plus options from `uvnbail` |
| `argv[2]` | the archive file to create or modify |
| `argv[3…]` | the files / members to act on (may be empty for `t`, `p`, `x`) |

Exactly one command letter is required. Giving two is an error (`ar: only one of [mrxtdpq]
allowed`); giving none is an error (`ar: must be one of [mrxtdpq]`) — except that a lone `u`
with no command is treated as `r`, so `ar u lib.a f.o` means "replace if newer".

The usage line printed on a bad invocation is:

```
Usage: ar [uvnbail][mrxtdpq] archive file...
```

**The positioning argument.** The insert options `a`, `b`, and `i` ([§4](#4-option-letters))
splice new members next to an existing one. That existing member's name is supplied as an
**extra argument that comes *before* the archive name**, shifting everything right by one:

```
ar rb  existing.o  lib.a  new.o        # insert new.o before member existing.o
ar ma  first.o     lib.a  last.o       # move last.o to just after first.o
```

Without a positioning option the layout is the plain `flags archive files…`.

### 2.1 Exit status

| Status | Meaning |
|--------|---------|
| `0` | success; every named file was found and handled |
| `N > 0` | the number of named files that were requested but **not found** in the archive |
| `1` | a fatal error: bad flag, missing arguments, not-an-archive, or a write failure |
| `100` | an interrupt signal (`SIGHUP`/`SIGINT`/`SIGQUIT`) aborted the run |

The "files not found" count comes from `report_missing()`: each command nulls out a name as it
handles it, and anything still pending at the end is reported (`ar: <name> not found`) and
counted into the exit status.

---

## 3. Command letters

Choose exactly one. Except for `q`, none of these edits the archive in place: the old archive is
streamed member-by-member into a temporary file, the change is applied along the way, and the
temp is copied back over the original as the last step (see [§5](#5-how-it-works-internally)).

### 3.1 `r` — replace or add

```
ar r  lib.a  f1.o f2.o ...
```

Streams the existing archive. For each old member that matches one of the named files, the fresh
copy is read from disk and written in place of the old one; unmatched old members are copied
through unchanged. Any named file that was **not** already a member is appended at the end. If the
archive does not yet exist it is created (with an `ar: creating lib.a` notice unless `c` is
given). With no named files, `r` refreshes every member from its like-named file on disk.

With `u` ([§4](#4-option-letters)), a member is replaced only if the on-disk file is **newer**
than the stored copy (its modification time is compared against the member's `ar_date`); otherwise
the old member is kept.

With `a`/`b`/`i`, the newly added files are inserted at the named position instead of at the end
([§5.2](#52-positioned-insertion)).

### 3.2 `d` — delete

```
ar d  lib.a  f1.o f2.o ...
```

Copies every member into the temp **except** the named ones, then commits. Naming a file that is
not in the archive is not fatal — it is simply reported and counted at exit. The archive must
already exist (`ar: lib.a not found` otherwise).

### 3.3 `x` — extract

```
ar x  lib.a  [f1.o f2.o ...]
```

Recreates each named member as a real file in the current directory, with the permission bits
stored in the header. With no names, **every** member is extracted. The archive itself is not
modified. When specific names are given, scanning stops as soon as the last one has been
extracted. Note that an extracted file carries the member's stored, word-padded length, so it may
be a few zero bytes longer than the original (see [§6](#6-the-archive-file-format)).

### 3.4 `t` — table of contents

```
ar t   lib.a  [names ...]      # one member name per line
ar tv  lib.a  [names ...]      # long, ls -l style listing
```

Lists the named members, or all of them if none are named. Read-only. With `v`, each line is a
full `ls -l`-style entry: a nine-character `rwxrwxrwx` permission string, `uid/gid`, the size in
bytes, and the formatted modification date, e.g.:

```
rw-r--r--501/20   1536 Jul  2 14:03 2026 util.o
```

### 3.5 `p` — print

```
ar p   lib.a  [names ...]
ar pv  lib.a  [names ...]      # precede each member with a "<name>" banner
```

Writes the raw bytes of the named members (or all members) to standard output. Nothing is
modified. With `v`, each member's contents are preceded by a `\n<name>\n\n` banner.

### 3.6 `m` — move

```
ar m   lib.a  f1.o f2.o ...           # move named members to the end
ar mb  before.o  lib.a  f1.o ...      # move them to just before before.o
ar ma  after.o   lib.a  f1.o ...      # move them to just after after.o
```

Repositions members within the archive without changing their contents. The named members are set
aside while the rest are streamed through; they are then reinserted, either at the end or, with a
positioning option, next to the named member ([§5.2](#52-positioned-insertion)).

### 3.7 `q` — quick append

```
ar q  lib.a  f1.o f2.o ...
```

Appends the named files to the **end** of the archive without reading or rewriting what is already
there — fast, but it does **no** duplicate checking (a member of the same name may end up stored
twice). Unlike the other commands, `q` writes the real archive directly rather than through a temp
file, creating it (with a warning unless `c` is given) if it does not exist. `q` is incompatible
with the positioning options; `ar qa …` fails with `ar: abi and q incompatible`.

---

## 4. Option letters

Options may appear anywhere in the flag string. They are cumulative and harmless when irrelevant
to the chosen command.

| Letter | Name | Effect |
|--------|------|--------|
| `u` | update | With `r`, replace a member only if the disk file is newer than the stored `ar_date`. A lone `u` with no command letter is treated as `r`. |
| `v` | verbose | Trace each action as `<letter> - <name>` (`r` replaced, `a` appended, `d` deleted, `x` extracted, `m` moved, `q` quick-appended). With `t` it selects the long listing; with `p` it prints per-member banners. |
| `vv` | very verbose | Repeating `v` also traces the `c` ("copied unchanged") members, which are normally silent to avoid drowning the output on a large archive. |
| `a` | after | Positioned insert: place added/moved members **after** the named member (see [§2](#2-invoking-the-archiver) for the extra argument). |
| `b` | before | Positioned insert: place them **before** the named member. |
| `i` | insert | Synonym for `b`. |
| `l` | local | Put the working temp files in the current directory instead of `/tmp` (useful when `/tmp` is small or on a different filesystem). |
| `c` | create | Suppress the `ar: creating <archive>` notice printed the first time an archive is created. **Not listed in the usage string**, but accepted. |
| `n` | — | Accepted for command-line compatibility and ignored. |

---

## 5. How it works internally

### 5.1 The streaming / temp-file model

The commands `r d x t p m` never modify the archive in place. Each opens the archive read-only and
walks it one member at a time (`next_member()` reads the next header; `copy_member()` moves or
skips the member's bytes). Mutating commands write the result into a fresh temporary file that was
seeded with the `ARMAG` magic word, then `commit_archive()` recreates the real archive and copies
the temp back over it. This "rebuild then replace" approach means an interrupted run never
corrupts the original — the half-written data is only ever in the temp.

Three temp templates are used, created with `mkstemp()`:

| Template | Role |
|----------|------|
| `/tmp/ar0XXXXXX` | the main rebuild temp |
| `/tmp/ar1XXXXXX` | the "before" staging temp, for positioned insertion |
| `/tmp/ar2XXXXXX` | the "move" staging temp, for the `m` command |

With the `l` option these become `ar0XXXXXX`/`ar1XXXXXX`/`ar2XXXXXX` in the current directory. All
temps are removed on the way out through the single exit path `finish()`, whether the run
succeeds, hits a fatal error, or is interrupted.

The `q` command is the sole exception: it opens the real archive read-write, seeks to the end, and
appends directly. Because a partial append is better completed than aborted, `q` ignores interrupt
signals while it writes.

### 5.2 Positioned insertion

The `a`/`b`/`i` options let `r` and `m` splice members next to a named "position" member rather
than at the end. This is handled by a small state machine (`handle_position()`) run once per
existing member while streaming:

1. Stream members into the main temp until the position member is seen.
2. At the splice point, open the **before-temp** and redirect all *remaining* old members into it.
   New members (written to the main temp) therefore land *between* the two halves.
3. `-a` ("after") lets the position member itself through into the main temp first, so the splice
   falls just after it; `-b`/`-i` ("before") splices just ahead of it.

For `m`, matched members are diverted into the **move-temp** as they are encountered. At commit
time `commit_archive()` concatenates the pieces in order — **main temp, then move-temp, then
before-temp** — reproducing the archive with the moved members dropped in at the splice point.

### 5.3 Name matching and "not found" reporting

A named file matches a member when their basenames are equal (`match_member()`). Each command
loop nulls out a name once it has been handled, so a name can match at most once, and any name
left un-nulled at the end was requested but never found — these are printed as `ar: <name> not
found` and their count becomes the exit status ([§2.1](#21-exit-status)).

---

## 6. The archive file format

An archive is the magic word `ARMAG` stored as one full 6-byte word, followed by a sequence of
members. Each member is a **variable-size, word-aligned header** immediately followed by the
member's data, which is zero-padded up to a whole word:

```
  offset 0:   ARMAG                         one word  (6 bytes)
  offset 6:   ar_hdr of member 1            arhdrsz(&h1) bytes (a multiple of 6)
              member 1 data + zero padding  ar_size bytes (a multiple of 6)
              ar_hdr of member 2            arhdrsz(&h2) bytes
              member 2 data + zero padding  ...
              ...
```

The constants live in [`cross/besm6/ar.h`](../cross/besm6/ar.h):

```c
#define ARMAG     0177545      /* archive magic, stored as one 6-byte word */
#define ARMAXNAME 255          /* longest member name, in bytes */

struct ar_hdr {
    char   *ar_name;           /* malloc'd, NUL-terminated (caller-owned) */
    word_t  ar_date;           /* modification time */
    word_t  ar_uid;            /* owner user id */
    word_t  ar_gid;            /* owner group id */
    word_t  ar_mode;           /* permission bits */
    word_t  ar_size;           /* data size in bytes — already word-padded */
};

int arhdrsz(const struct ar_hdr *h);   /* on-disk header size for h->ar_name */
```

### 6.1 Header on-disk layout

The member name is **length-prefixed** on disk — a single length byte (1..255) followed by that
many name bytes — and the name is zero-padded so that the length byte plus name occupy a whole
number of words; the five metadata words follow. Every multi-byte quantity is **big-endian** (most
significant byte first). For a name of `L` bytes:

| Byte range | Bytes | Field | Notes |
|------------|-------|-------|-------|
| 0 | 1 | length | name length `L`, 1..255 |
| 1 … L | L | `ar_name` | the name, no NUL terminator |
| L+1 … | pad | padding | zero bytes rounding `1 + L` up to a multiple of 6 |
| … +0 | 6 | `ar_date` | full 48-bit word |
| … +6 | 6 | `ar_uid` | value in the low half-word, high half zero |
| … +12 | 6 | `ar_gid` | value in the low half-word, high half zero |
| … +18 | 6 | `ar_mode` | value in the low half-word, high half zero |
| … +24 | 6 | `ar_size` | full 48-bit word: the padded data length |

So the whole header occupies `arhdrsz(&h) = roundup(1 + L, 6) + 5·6` bytes, always a multiple of 6.
In memory `ar_name` is a malloc'd, NUL-terminated string: `fgetarhdr()`/`getarhdr()` allocate it and
the caller must `free()` it; `putarhdr()` only reads it (it may point at a borrowed string). The
header is written by `putarhdr()` and read by `getarhdr()` in [`cmd/libaout/`](../cmd/libaout/); the
magic word and any bare integer go through `putint()`/`getint()`, which write/read one 6-byte
big-endian word. These share the identical layout with the linker's `fput*`/`fget*` helpers — see
[Linker_Manual.md §8.6](Linker_Manual.md#86-word-encoding-and-the-libaout-helpers).

### 6.2 Word padding and the stepping invariant

`b6ar` pads each member's data with zero bytes up to a multiple of `W` (6), and stores the
**padded** length in `ar_size` (`copy_member()` computes `pad = (W - size % W) % W` and records
`size + pad`). Two consequences follow:

- Because the header size is itself a multiple of `W`, every member header lands on a word
  boundary, so the whole archive is a whole number of words.
- A reader can step from one member to the next by exactly `ar_size + arhdrsz(&h)` bytes with **no
  rounding** — which is precisely what the linker does. The archiver's unit tests
  (`MembersWordAligned` in [`cmd/ar/test/ar_test.cpp`](../cmd/ar/test/ar_test.cpp)) verify this
  invariant by re-reading the produced archive with the same `getarhdr`/`getint` decoders the
  linker uses.

Because the stored length is the padded one, `x` (extract) writes a file that may carry a few
trailing zero bytes beyond the original content. For object-file members this is harmless: the
object's own header records its true segment sizes.

---

## 7. Symbol tables and `b6ranlib`

`b6ar` itself never builds a symbol index — it only bundles member files. A plain archive is
searched by the linker member-by-member: a member is linked in only if it defines a symbol that is
currently needed.

To speed searching, the companion tool `b6ranlib` turns a plain archive into a **randomized
archive** by prepending a special member named `__.SYMDEF`: a table of contents pairing every
exported symbol with the byte offset of the member that defines it. Its entries are `struct
ranlib` records ([`cross/besm6/ranlib.h`](../cross/besm6/ranlib.h)):

```c
struct ranlib {
    word_t  ran_len;       /* 1 byte  - length of the name */
    word_t  ran_off;       /* offset of the defining member */
    char *  ran_name;      /* the exported symbol name */
};
```

The linker loads this table and pulls in defining members directly, iterating to a fixed point,
and falls back to a member-by-member scan if the table is older than the archive. The full story —
selective loading, randomized archives, library search (`-lname`) — is in
[Linker_Manual.md §7](Linker_Manual.md#7-archives-and-libraries), and the `ranlib` on-disk
encoding in [§8.7](Linker_Manual.md#87-archive-and-ranlib-structures). `b6ar` treats a
`__.SYMDEF` member like any other member; deleting or replacing members invalidates it, so re-run
`b6ranlib` after editing a randomized archive.

---

## 8. Diagnostics and exit status

All messages are written to standard error and prefixed with `ar:`.

| Message | Cause |
|---------|-------|
| ``ar: unknown flag `X' `` | an unrecognized letter in the flag string (fatal, exit 1) |
| `ar: must be one of [mrxtdpq]` | no command letter and no lone `u` (fatal) |
| `ar: only one of [mrxtdpq] allowed` | two command letters given (fatal) |
| `ar: <archive> is not in archive format` | the file exists but its first word is not `ARMAG` (fatal) |
| `ar: <archive> not found` | a read command (`d x t p m`) named a nonexistent archive (fatal) |
| `ar: creating <archive>` | an archive was created for the first time (notice; suppressed by `c`) |
| `ar: cannot open <file>` | an input file could not be read |
| `ar: cannot create <file>` | `x` could not create the output file |
| `ar: <name> not found` | a named member was requested but not present (counted into exit status) |
| `ar: phase error on <name>` | a member was shorter than its header claimed — a truncated or corrupt archive |
| `ar: abi and q incompatible` | a positioning option was combined with `q` (fatal) |
| `ar write error: …` | a write failed (disk full, etc.); reported via `perror` (fatal) |
| `ar: cannot create temporary file` | `mkstemp()` failed for a rebuild/staging temp (fatal) |

Fatal errors exit with status `1`. Interrupt signals (`SIGHUP`, `SIGINT`, `SIGQUIT`) exit with
`100` after removing the temp files; they are trapped only if the shell had not already set them to
be ignored. Otherwise the exit status is the count of named files not found
([§2.1](#21-exit-status)).

---

## 9. Worked example

Build a library from three object files, inspect it, and edit it:

```console
$ b6ar rc libutil.a  alloc.o  string.o  io.o     # create, no "creating" notice
$ b6ar t libutil.a                               # table of contents
alloc.o
string.o
io.o
$ b6ar tv libutil.a                              # long listing
rw-r--r--501/20    768 Jul  2 14:01 2026 alloc.o
rw-r--r--501/20   1536 Jul  2 14:01 2026 string.o
rw-r--r--501/20    984 Jul  2 14:02 2026 io.o
```

Refresh one member from a freshly rebuilt object, but only if it is newer:

```console
$ b6ar ru libutil.a string.o                     # replace-if-newer
```

Insert a new member just before another, delete one, and move one to the end:

```console
$ b6ar rb io.o  libutil.a  trace.o               # trace.o goes before io.o
$ b6ar d libutil.a alloc.o                        # remove alloc.o
$ b6ar mv libutil.a string.o                      # move string.o to the end (verbose)
m - string.o
```

Extract a member and index the library for the linker:

```console
$ b6ar x libutil.a io.o                           # recreate io.o in the cwd
$ b6ranlib libutil.a                              # add the __.SYMDEF table of contents
$ b6ld -o prog  crt0.o  main.o  libutil.a         # link against the library
```

---

## 10. Source pointers

| File | Responsibility |
|------|----------------|
| [`cmd/ar/main.c`](../cmd/ar/main.c) | command-line front end; calls `ar_run()` |
| [`cmd/ar/ar.c`](../cmd/ar/ar.c) | flag parsing, command dispatch, temp-file setup, signal trapping |
| [`cmd/ar/command.c`](../cmd/ar/command.c) | the seven command handlers (`r d x t p m q`) |
| [`cmd/ar/archive.c`](../cmd/ar/archive.c) | member streaming, `copy_member`, positioned insertion, commit |
| [`cmd/ar/list.c`](../cmd/ar/list.c) | the `t -v` long listing and permission formatting |
| [`cmd/ar/util.c`](../cmd/ar/util.c) | the `finish()` exit path, diagnostics, `basename_of`, missing-file reporting |
| [`cmd/ar/intern.h`](../cmd/ar/intern.h) | the `arstate` engine state, `W`, and the `SKIP/IODD/OODD/HEAD` copy flags |
| [`cmd/ar/archive.h`](../cmd/ar/archive.h) | the public `ar_run()` entry point (shared with the tests) |
| [`cmd/ar/test/ar_test.cpp`](../cmd/ar/test/ar_test.cpp) | round-trip, word-alignment, extract, and delete tests |
| [`cross/besm6/ar.h`](../cross/besm6/ar.h) | `ARMAG`, `ARMAXNAME`, `arhdrsz()`, and `struct ar_hdr` |
| [`cross/besm6/ranlib.h`](../cross/besm6/ranlib.h) | `struct ranlib` for the `__.SYMDEF` table of contents |
| [`cmd/libaout/`](../cmd/libaout/) | `getarhdr`/`putarhdr`/`getint`/`putint` — the shared big-endian word serialization |
