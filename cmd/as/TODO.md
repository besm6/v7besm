Tasks to do in assembler:

 * LD-2: The 15-bit long-address masks are fixed in pass1.c makecmd() and pass2.c adjust(), and the raw-opcode escapes (LSCMD $NN, LLCMD @NN) now emit the BESM-6 bit positions. STILL TODO: the matching long mask in cmd/ld/ld.c relhalf() is still 20-bit (03777777) and must become 15-bit (077777); deferred with the rest of ld.

 * DISASM-6: Word-length migration is nearly done. cmd/as + cross/besm6/b.out.h (HDRSZ) + cmd/libaout (fputh/fgeth/fgetsym) and now cmd/ld/{ld.c,size.c,ranlib.c} use the BESM-6 48-bit word (W=6, 3-byte half-words, HDRSZ=54). STILL TODO: cmd/disasm/dis.c still `#define W 8`, so it reads files inconsistent with what `as`/`ld` now emit. Migrate it to W=6 (and any hard-coded 8/72 byte offsets) before it can interoperate.

 * AR-10: copyfil() in cmd/ld/ar.c pads archive member *data* to an even byte count (the IODD/OODD flags, a PDP-11 2-byte-word legacy) instead of to a 6-byte BESM-6 word. The header is now fully word-aligned (ar_name[30], ARHDRSZ=60) and ld advances by `archdr.ar_size + ARHDRSZ`, so member data must also be padded to a whole 6-byte word for member offsets to stay word-aligned. Replace the even-byte padding in copyfil() (and the matching size accounting) with rounding to a multiple of W=6.

 * AS-12: The decimal suffix `d`/`D` misparses. Per doc/Assembler_Manual.md §6.1 `1234d` should be decimal 1234 (== 02322), but lex.c yields 030101 instead (bare `1234` is correct). The number scanner mishandles the trailing `d` (likely treating it as a hex digit and/or not committing to base 10). Fix getnum()/the suffix handling in lex.c so `digits` `d`/`D` is plain decimal.

 * AS-13: `.ascii` always appends padding, even when the byte count is already a multiple of W. makeascii() in pass1.c computes `c = W - n % W` without the `% W` guard, so `n % W == 0` yields a full extra zero word (e.g. `.ascii "ABCDEF"` emits two words: the text and an all-zero word). Decide whether a trailing NUL word is intended; if not, make the pad `(W - n % W) % W`.

 * AS-14: typesegm[] in tables.c has the same non-contiguous-index defect that was just fixed in segmtype[]/segmrel[], but in the *type* index space: N_BSS=05, N_ABSS=06, N_STRNG=07 leave the array short, so TYPESEGM(N_STRNG) reads out of bounds and TYPESEGM(N_ABSS) returns SSTRNG. No current input exercises it (it bites a symbol whose type is N_STRNG, i.e. a label defined in the .strng segment). Fix: insert the N_ABSS placeholder at index 6 so N_STRNG lands at index 7, mirroring the segmtype[]/segmrel[] fix.
