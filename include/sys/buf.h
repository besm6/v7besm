// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Each buffer in the pool is usually doubly linked into 2 lists:
// the device with which it is currently associated (always)
// and also on a list of blocks available for allocation
// for other use (usually).
// The latter list is kept in last-used order, and the two
// lists are doubly linked to make it easy to remove
// a buffer from one list when it was found by
// looking through the other.
// A buffer is on the available list, and is liable
// to be reassigned to another disk block, if and only
// if it is not marked BUSY.  When a buffer is busy, the
// available-list pointers can be used for other purposes.
// Most drivers use the forward ptr as a link in their I/O
// active queue.
// A buffer header contains all the information required
// to perform I/O.
// Most of the routines which manipulate these things
// are in bio.c.

#ifndef _SYS_BUF_H
#define _SYS_BUF_H

struct buf {
    int b_flags;         // see defines below
    struct buf *b_forw;  // headed by d_tab of conf.c
    struct buf *b_back;  // "
    struct buf *av_forw; // position on free list,
    struct buf *av_back; // if not BUSY
    dev_t b_dev;         // major+minor device name
    int b_wcount;        // transfer count, in WORDS
    daddr_t b_blkno;     // block # on device
    int b_error;         // returned after I/O
    int b_resid;         // words not transferred after error

    // Core address of the block's data.  
    // v7 spelled this a union of a caddr_t and one word-pointer type per
    // kind of block (b_filsys/b_dino/b_dir/b_daddr); those are gone, because on this machine
    // they were a footgun.  binit() fills b_addr with a FAT pointer (bit-48 marker, byte
    // offset in 47-45), and reading it back through a plain word pointer keeps the marker:
    // b6cc adds a field offset to the whole 48-bit value on the additive unit -- which reads
    // bit 48 as a floating exponent -- BEFORE truncating to the 15-bit word address, so any
    // offset != 0 collapses back and silently returns word 0 of the block, with no fault
    // (offset 0 alone survives).  To read the block as a struct, cast b_addr, which clears the
    // marker: `((struct filsys *)bp->b_addr)->s_bsize' (doc/Besm6_Data_Representation.md §7).
    caddr_t b_addr; 

    // Where the data lives, physically, in WORDS -- valid only when B_PHYS.
    // b_addr cannot serve: a caddr_t is a fat pointer whose word field is
    // 15 bits, so it cannot name anything above 32767, and physical memory runs
    // to 512 Kwords.  Filled by swap() and physio(); read through bufpaddr().
    paddr_t b_paddr;
};

extern struct buf buf[];     // The buffer pool itself
extern struct buf bfreelist; // head of available list

// These flags are kept in b_flags.
#define B_WRITE  0     // non-read pseudo-flag
#define B_READ   01    // read when I/O occurs
#define B_DONE   02    // transaction finished
#define B_ERROR  04    // transaction aborted
#define B_BUSY   010   // not on av_forw/back list
#define B_PHYS   020   // physical I/O
#define B_WANTED 0100  // issue wakeup when BUSY goes off
#define B_AGE    0200  // delayed write for correct aging
#define B_ASYNC  0400  // don't wait for I/O completion
#define B_DELWRI 01000 // don't write till block leaves available list
#define B_TAPE   02000 // this is a magtape (no bdwrite)
#define B_PBUSY  04000
#define B_PACK   010000

// Where a request's data lives, physically, in words -- the one call a strategy
// routine makes to find its transfer target, and the only place B_PHYS is read.
//
// A kernel buffer is unmapped, so its address IS its physical address and the word
// field of the fat pointer is the whole answer.  A B_PHYS request carries the address
// explicitly, because its target may be above the 32767 that field can reach.
#define bufpaddr(bp) ((bp)->b_flags & B_PHYS ? (bp)->b_paddr : ptrword((bp)->b_addr))

// special redeclarations for
// the head of the queue per
// device driver.
//
// A device header never has B_PHYS set, so b_paddr is unused there and needs no
// alias of its own -- do not add one, or bufpaddr() stops meaning one thing.
#define b_actf   av_forw
#define b_actl   av_back
#define b_active b_wcount
#define b_errcnt b_resid

#endif // _SYS_BUF_H
