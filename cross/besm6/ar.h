#include "besm6/types.h"

#define ARMAG     0177545
#define ARMAXNAME 255 // longest member name, in bytes

//
// Member name is length-prefixed on disk (1 length byte + up to ARMAXNAME name
// bytes, zero-padded to a whole BESM-6 word), so the header is variable-sized;
// use arhdrsz() to obtain a member's on-disk header size.  In memory ar_name is
// a malloc'd, NUL-terminated string owned by the caller (like struct ranlib's
// ran_name): fgetarhdr()/getarhdr() allocate it and the caller must free() it;
// putarhdr() only reads it (it may point at a borrowed string).
//
struct ar_hdr {
    char *ar_name;
    word_t ar_date;
    word_t ar_uid;
    word_t ar_gid;
    word_t ar_mode;
    word_t ar_size;
};

// On-disk size in bytes of the member header for the name in *h.
int arhdrsz(const struct ar_hdr *h);

int fgetarhdr(FILE *f, struct ar_hdr *h);

//
// File-descriptor counterparts used by ar/ranlib for in-place archive I/O.
// They use the same on-disk field layout as fgetarhdr().
//
int getarhdr(int f, struct ar_hdr *h);
int putarhdr(int f, const struct ar_hdr *h);
