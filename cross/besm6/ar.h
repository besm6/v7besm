
#define ARMAG   0177545
#define ARHDRSZ 56

struct ar_hdr {
    char    ar_name[14];
    long    ar_date;
    int     ar_uid;
    int     ar_gid;
    int     ar_mode;
    long    ar_size;
};

int fgetarhdr(FILE *f, struct ar_hdr *h);

/*
 * File-descriptor counterparts used by ar/ranlib for in-place archive I/O.
 * They use the same on-disk field layout as fgetarhdr().
 */
int getarhdr(int f, struct ar_hdr *h);
int putarhdr(int f, const struct ar_hdr *h);
