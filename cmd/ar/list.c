//
// BESM-6 archiver: verbose table-of-contents listing and permission formatting.
//
#include <stdio.h>
#include <time.h>

#include "intern.h"

static void print_perm_bits(void);             // file-local; defined below
static void print_perm_char(const int *pairp); // file-local; defined below

// Permission and file-type bits, matching the on-disk st_mode layout (octal).
#define IFMT  060000 // mask for the file-type field (unused here)
#define ISARG 01000  // "argument" bit (unused here)
#define LARGE 010000 // large-file bit (unused here)
#define SUID  04000  // set-user-id on execution
#define SGID  02000  // set-group-id on execution
#define ROWN  0400   // owner read
#define WOWN  0200   // owner write
#define XOWN  0100   // owner execute
#define RGRP  040    // group read
#define WGRP  020    // group write
#define XGRP  010    // group execute
#define ROTH  04     // other read
#define WOTH  02     // other write
#define XOTH  01     // other execute
#define STXT  01000  // sticky ("save text") bit

// Print the long, ls -l style listing line for the current member (t -v).
//
// Shows the permission string, owner/group ids, size, and formatted date. The
// ctime() string is chopped up with printf field widths to pick out the
// "Mon DD HH:MM" and year portions.
void print_long_entry(void)
{
    const char *cp;
    time_t t;

    print_perm_bits();
    printf("%3d/%1d", (int)ar.hdr.ar_uid, (int)ar.hdr.ar_gid);
    printf("%7ld", (long)ar.hdr.ar_size);
    t  = ar.hdr.ar_date;
    cp = ctime(&t);
    printf(" %-12.12s %-4.4s ", cp + 4, cp + 20);
}

// Permission-display tables. Each row has the form
//     { count, mask, char, mask, char, ... }
// print_perm_char() tests the masks in order and prints the char paired with
// the first mask that is set, or the final fallback char if none match. The
// nine rows drive the nine columns of an "rwxrwxrwx" permission string.
static int m1[] = { 1, ROWN, 'r', '-' };            // owner read
static int m2[] = { 1, WOWN, 'w', '-' };            // owner write
static int m3[] = { 2, SUID, 's', XOWN, 'x', '-' }; // owner exec / setuid
static int m4[] = { 1, RGRP, 'r', '-' };            // group read
static int m5[] = { 1, WGRP, 'w', '-' };            // group write
static int m6[] = { 2, SGID, 's', XGRP, 'x', '-' }; // group exec / setgid
static int m7[] = { 1, ROTH, 'r', '-' };            // other read
static int m8[] = { 1, WOTH, 'w', '-' };            // other write
static int m9[] = { 2, STXT, 't', XOTH, 'x', '-' }; // other exec / sticky

static int *m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 }; // the nine columns in order

// Print the nine-character "rwxrwxrwx" permission field for the current member.
static void print_perm_bits(void)
{
    int **mp;

    for (mp = &m[0]; mp < &m[9];)
        print_perm_char(*mp++);
}

// Print one character of the permission string from a table row.
//
// The row starts with a count of (mask, char) pairs to try. The first pair
// whose mask is set in ar_mode wins; if none match, the char just past the last
// pair is the fallback (typically '-').
static void print_perm_char(const int *pairp)
{
    int n;
    const int *ap;

    ap = pairp;
    n  = *ap++;
    while (--n >= 0 && (ar.hdr.ar_mode & *ap++) == 0)
        ap++;
    putchar(*ap);
}
