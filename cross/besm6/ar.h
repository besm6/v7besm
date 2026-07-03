#include "besm6/types.h"

#define ARMAG   0177545
#define ARHDRSZ 60

struct ar_hdr {
    char ar_name[30];
    word_t ar_date;
    word_t ar_uid;
    word_t ar_gid;
    word_t ar_mode;
    word_t ar_size;
};

int fgetarhdr(FILE *f, struct ar_hdr *h);

//
// File-descriptor counterparts used by ar/ranlib for in-place archive I/O.
// They use the same on-disk field layout as fgetarhdr().
//
int getarhdr(int f, struct ar_hdr *h);
int putarhdr(int f, const struct ar_hdr *h);
