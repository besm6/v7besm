# Recognizing BESM-6 binaries with `file(1)`

The BESM-6 toolchain (`cmd/as`, `cmd/ld`, and the binutils) emits a
BESM-6-specific `a.out` variant defined in [`cross/besm6/b.out.h`](../cross/besm6/b.out.h)
(see also [Linker_Manual.md](Linker_Manual.md) for the object/executable
format). Out of the box `/usr/bin/file` does not know this format and reports
these files as `data`. This note explains how to teach `file` to recognize
them, so that:

```
$ file hello main.o
hello:  besm6 a.out executable
main.o: besm6 a.out object
```

## What the format looks like on disk

Every header field is one 48-bit BESM-6 word = **6 bytes**, stored big-endian.
`fputw()` writes each field as 3 zero-padding bytes followed by a 3-byte value
(see [`cmd/libaout/fputw.c`](../cmd/libaout/fputw.c) and
[`cmd/libaout/fputhdr.c`](../cmd/libaout/fputhdr.c)). The header is 9 words =
54 bytes (`HDRSZ`).

Two fields identify a file:

- **Bytes 0–5, `a_magic`** — the ASCII string `BESM` followed by a 2-byte tail
  that names the file kind:

  | Tail (hex) | Octal | Constant  | Meaning                          |
  |------------|-------|-----------|----------------------------------|
  | `0x0107`   | 0407  | `FMAGIC`  | relocatable / impure             |
  | `0x0108`   | 0410  | `NMAGIC`  | pure executable                  |

  So a big-endian 16-bit read (`beshort`) at **offset 4** distinguishes the
  two magics.

- **`a_flag`** is the 9th header word (offset 48). Its low value byte lands at
  **offset 53**. The `RELFLG` bit (`0x01`, see `b.out.h`) is **set** in a fully
  linked file that carries no relocation records (an *executable*) and **clear**
  in a relocatable object that still has relocation records (an *object*).

That gives the discrimination:

- `NMAGIC` → always an executable (pure).
- `FMAGIC` → check `RELFLG` at byte 53: set → executable, clear → object.

## The magic rules

```text
# BESM-6 a.out object/executable files (cross/besm6/b.out.h)
0	string	BESM	besm6 a.out
# a_magic tail selects the file kind
>4	beshort	0x0108	pure executable
>4	beshort	0x0107
# FMAGIC: RELFLG bit (a_flag, byte 53) tells linked exe from relocatable object
>>53	byte&0x01	1	executable
>>53	byte&0x01	0	object
```

The columns are **tab-separated** (offset, type, test value, message) — this is
required by `file`; do not replace the tabs with spaces. The leading `BESM`
line prints `besm6 a.out`; each nested `>` / `>>` line appends the kind:

| File                                   | Output                                   |
|----------------------------------------|------------------------------------------|
| FMAGIC, `RELFLG` set (linked)          | `besm6 a.out executable`                 |
| FMAGIC, `RELFLG` clear (relocatable)   | `besm6 a.out object`                     |
| NMAGIC                                 | `besm6 a.out pure executable`            |

## Setup

`file` automatically loads a personal magic file named `~/.magic` (in addition
to the compiled system database). You can confirm your build does so:

```sh
$ file --version
file-5.41
magic file from /Users/you/.magic:/usr/share/file/magic.mgc
```

Append the rules above to `~/.magic` (create the file if it does not exist).
That is all — no recompilation is needed:

```sh
$ file hello main.o
hello:  besm6 a.out executable
main.o: besm6 a.out object
```

### Using an explicit magic file

To keep the rules with the project instead of in `~/.magic`, save them to a
file (say `besm6.magic`) and point `file` at it:

```sh
file -m besm6.magic hello main.o
```

### Compiling for speed

For a large tree you can precompile the rules into a `.mgc` database and select
it with `-m` or the `MAGIC` environment variable:

```sh
file -C -m ~/.magic          # writes ~/.magic.mgc
MAGIC=~/.magic.mgc file hello main.o
```
