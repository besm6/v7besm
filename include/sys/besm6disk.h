/*
 * The BESM-6 mass-storage exchange control word: how a drum (МБ) or a disk (МД/КМД)
 * transfer is described in the accumulator of the 033 «увв» instruction that starts it.
 *
 * WHERE THE LINE IS, between this file and sys/besm6dev.h.  That file owns the machine's
 * two GLOBAL namespaces -- the 033/002 address map and the ГРП/ПРП bit map -- where every
 * device competes for the same numbers and centralising them is the whole point.  This
 * file owns one device family's ACCUMULATOR LAYOUT, which is nobody else's business: the
 * mass-storage devices are the only ones whose payload is a structured word rather than a
 * character or a flag, and besm6dev.h is included by kernel/besm6.S and by eleven files
 * under kernel/test/ that have no use for any of this.
 *
 * The drum and the disk share the whole upper half of the layout -- CW_BLOCK, CW_PAGE,
 * CW_READ, CW_PAGE_MODE, CW_UNIT are the same bits in the same places on both -- which is
 * what makes one header sensible even though the two devices get separate drivers
 * (dev/mb.c and dev/md.c, whose headers carry the reasoning).
 *
 * Addresses and bit values are OCTAL; bits are numbered right-to-left from 1, so bit N
 * has the value 2^(N-1).  See doc/Besm6_Peripherals.md, "Magnetic drums" and "Magnetic
 * disks", and doc/Intrinsics.md §6.3 for a drum page read written out in C.
 *
 * The disk's second step -- the 033 023 controller commands -- and its status register
 * are NOT here.  They are a different protocol layer and belong with the code that
 * exercises them: both now live in kernel/dev/md.c, as MDCMD_* and MDST_*.  That file is
 * also where to find which status bits this simulator computes correctly, which is fewer
 * than the hardware documentation lists and not the ones you would guess.
 */
#ifndef _SYS_BESM6DISK_H
#define _SYS_BESM6DISK_H

/*
 * The fields both devices share.
 */
#define CW_BLOCK        0740000000 /* 27-24: memory block number */
#define CW_READ_SYSDATA 004000000  /* 21: transfer the 8 service words only */
#define CW_PAGE_MODE    001000000  /* 19: 1 = a whole 1024-word page, 0 = less */
#define CW_READ         000400000  /* 18: 1 = device -> memory, 0 = memory -> device */
#define CW_PAGE         000370000  /* 17-13: memory page number */
#define CW_UNIT         000001600  /* 10-8: unit number */

/*
 * Where the transfer lands, from a PHYSICAL PAGE NUMBER -- not a word address.
 *
 * Nine bits of page number reach all 512 Kwords of memory, but they are split across two
 * non-adjacent fields: the low 5 go to CW_PAGE at bit 13 (hence << 12) and the high 4 to
 * CW_BLOCK at bit 24 (hence << 23).
 *
 * A page is the finest granularity this macro can express, and that is the hardware's
 * doing, not a simplification: below it the two devices diverge -- the disk has
 * DISK_HALFPAGE (512 words) and the drum has DRUM_PARAGRAF (256 words, sector mode only)
 * -- so the sub-page field is each driver's own business.  Taking a word address here
 * would silently discard its low 10 bits.
 */
#define cwpage(pg) ((((pg) & 037) << 12) | (((pg) >> 5 & 017) << 23))

/*
 * Drum-only fields (033 1, 033 2).  The zone number is DRUM_UNIT and DRUM_CYLINDER read
 * together -- they are adjacent, and the pair IS the zone address, at bit 3 (>> 2).
 */
#define DRUM_READ_OVERLAY 020000000 /* 23: read with overlay -- not implemented in SIMH */
#define DRUM_PARITY_FLAG  010000000 /* 22: suppress words with bad parity */
#define DRUM_PARAGRAF     000006000 /* 12-11: paragraph within the page (sector mode) */
#define DRUM_CYLINDER     000000174 /* 7-3: track on the drum */
#define DRUM_SECTOR       000000003 /* 2-1: sector within the track (sector mode) */

/*
 * Disk-only fields (033 3, 033 4).
 *
 * DISK_HALFZONE is a TRAP, and is named here only so that nobody re-derives it from the
 * hardware documentation and uses it.  The bit exists in the field layout, but the
 * simulator declares it and reads it nowhere: which half of the zone is transferred comes
 * from bit 1 of the SECOND step's track-address command (`c->track = cmd & 1' in
 * disk_ctl(), besm6_disk.c), not from the control word.  A driver that sets it here gets
 * the wrong half and no diagnostic.  dev/md.c builds the half-zone into MDCMD_TRACK, where
 * it belongs.  Same story for CW_UNIT above, on the disk: the unit comes from the
 * controller's unit-select command, and the field in the control word is dead.
 *
 * DISK_HALFPAGE, by contrast, is live and does exactly what it says -- but it names the
 * half of the MEMORY page, which is an independent choice from the half of the zone.
 */
#define DISK_HALFPAGE 000004000 /* 12: which half of the memory page, when not PAGE_MODE */
#define DISK_HALFZONE 000000001 /*  1: DEAD -- see above; use MDCMD_TRACK's bit 1 */

/*
 * A zone is 8 service words plus 1 Kword of data.  The data goes where the control word
 * says; the service words always land at a fixed low address, one buffer per controller.
 */
#define SYSDATA_DRUM1 010 /* 010-017 */
#define SYSDATA_DRUM2 020 /* 020-027 */
#define SYSDATA_DISK3 030 /* 030-037 */
#define SYSDATA_DISK4 040 /* 040-047 */

#endif /* _SYS_BESM6DISK_H */
