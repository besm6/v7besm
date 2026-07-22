// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Inode structure as it appears on a disk block.
//
// EXACTLY SIXTEEN WORDS, so INOPB (32) of them tile a 512-word block with nothing
// left over.  Eight words of metadata, then eight disk addresses: the split is on a
// power-of-two boundary, so `dp + 8' is the address array.
//
// v7 spelled the addresses `char di_addr[40]' -- 13 addresses packed 3 bytes each,
// because a PDP-11 daddr_t was 24 bits inside a 32-bit word.  Here a daddr_t is a
// whole 41-bit signed word (sys/types.h), so the packing bought nothing and cost a
// conversion at every read and write of an inode.  iexpand() and iupdat() open-coded
// v7's l3tol/ltol3 as a 3-bytes-and-a-zero loop over char *, which assumed a 4-byte
// in-core daddr_t; it is 6, so that loop was writing at the wrong stride into an
// array of the wrong size.  It is now a word copy.
//
// di_addr[0..5] are direct, [6] single indirect, [7] double indirect.  There is no
// triple indirect: see NLEVEL in sys/param.h for why it cannot be reached here.
// di_addr[0] doubles as di_rdev for device files, as in v7.

#ifndef _SYS_INO_H
#define _SYS_INO_H

struct dinode {
    int di_mode;            // 0: mode and type of file
    int di_nlink;           // 1: number of links to file
    int di_uid;             // 2: owner's user id
    int di_gid;             // 3: owner's group id
    off_t di_size;          // 4: number of bytes in file
    time_t di_atime;        // 5: time last accessed
    time_t di_mtime;        // 6: time last modified
    time_t di_ctime;        // 7: time created
    daddr_t di_addr[NADDR]; // 8..15: disk block addresses
};

// The layout IS the on-disk format: if this ever stops holding, INOPB is a lie and
// the i-list silently overlaps or wastes space -- which is exactly what the old
// `INOPB 8' against a 15-word struct was doing.  A build failure is a better way to
// find that out than a corrupted filesystem.
//
// _Static_assert, not the `extern int x[1 - 2*(cond)]' idiom: b6cc accepts a
// NEGATIVE array size without a word, so that trick is decorative here.  Checked.
_Static_assert(sizeof(struct dinode) == 16 * NBPW, "struct dinode must be 16 words");
_Static_assert(sizeof(struct dinode) * INOPB == BSIZE, "INOPB dinodes must tile a block");

#endif // _SYS_INO_H
