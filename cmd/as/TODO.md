Tasks to do in assembler:

 * LD-2: The 15-bit long-address masks are fixed in pass1.c makecmd() and pass2.c adjust(), and the raw-opcode escapes (LSCMD $NN, LLCMD @NN) now emit the BESM-6 bit positions. STILL TODO: the matching long mask in cmd/ld/ld.c relhalf() is still 20-bit (03777777) and must become 15-bit (077777); deferred with the rest of ld.

 * AR-10: copyfil() in cmd/ld/ar.c pads archive member *data* to an even byte count (the IODD/OODD flags, a PDP-11 2-byte-word legacy) instead of to a 6-byte BESM-6 word. The header is now fully word-aligned (ar_name[30], ARHDRSZ=60) and ld advances by `archdr.ar_size + ARHDRSZ`, so member data must also be padded to a whole 6-byte word for member offsets to stay word-aligned. Replace the even-byte padding in copyfil() (and the matching size accounting) with rounding to a multiple of W=6.
