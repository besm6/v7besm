/*
 * strings -- the str* and mem* family, index/rindex and swab.
 *
 * These are phase 2's bulk, and most of them came from the c-compiler's own library
 * rather than from v7 (lib/README.md).  What is under test here is therefore not the
 * algorithms so much as the FAT POINTER they all walk on: `char *' carries a 3-bit
 * byte offset in bits 47-45 above the word address, and incrementing it DECREASES that
 * offset until it wraps and the word address steps.  So every routine below is given
 * work that starts and ends in the middle of a word, not on the word boundary a
 * declared array begins at -- a loop that quietly walked words instead of bytes would
 * pass every aligned test and fail these.
 *
 * memmove earns its own attention: it chooses its direction with `d < s', and a fat
 * pointer does NOT sort as a plain word (two pointers into the SAME word compare
 * backwards, the offset being the more significant field).  The compiler lowers a
 * pointer relational through b$pdiff rather than comparing the words, and the two
 * overlap cases below are what proves it -- they overlap within one word.
 *
 * Like the rest of test/ it declares write() itself and carries its own put(), stdio
 * being phase 4; it does take strlen from libc, which is the point of the exercise.
 */
#include <string.h>

int write(int fd, char *buf, int n);

/* Not ANSI, so no header declares these three: see gen/index.c and gen/swab.c. */
char *index(const char *sp, char c);
char *rindex(const char *sp, char c);
void swab(const char *from, char *to, int n);

/* One string to the standard output, without stdio (phase 4). */
static void put(char *s)
{
    write(1, s, strlen(s));
}

/* Report a claim by name, so the .expected file reads as a checklist. */
static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

/* A string, quoted, so a trailing space or a lost NUL is visible in the diff. */
static void show(char *what, char *s)
{
    put(what);
    put(" \"");
    put(s);
    put("\"\n");
}

int main(int argc, char **argv, char **envp)
{
    char buf[48], b2[48];
    char *p, *q;
    int i;

    /* ---- strlen, strcpy, strcat, on and off the word boundary ---- */
    ok("strlen of the empty string", strlen("") == 0);
    ok("strlen counts bytes, not words", strlen("abcdefghijk") == 11);

    strcpy(buf, "abc");
    ok("strcpy returns its destination", strcpy(buf, "abc") == buf);
    strcat(buf, "defghi");
    show("strcpy+strcat", buf);
    ok("... and its length", strlen(buf) == 9);

    /* buf+1 is byte #1 of the first word: everything below crosses word ends. */
    strcpy(buf + 1, "0123456789ab");
    show("strcpy to an unaligned destination", buf + 1);
    ok("... left the byte before it alone", buf[0] == 'a');
    ok("... and terminated where it should", strlen(buf + 1) == 12);

    strcpy(b2, "xy");
    strcat(b2 + 1, "zzz");
    show("strcat onto an unaligned string", b2);

    /* ---- strncpy: truncation, and the NUL padding v7 promises ---- */
    for (i = 0; i < 12; i++)
        buf[i] = '#';
    strncpy(buf, "abc", 8);
    ok("strncpy pads with NUL to n", buf[3] == 0 && buf[7] == 0);
    ok("... and not past n", buf[8] == '#');
    strncpy(b2, "abcdefgh", 4);
    b2[4] = 0;
    show("strncpy truncates without a NUL", b2);

    strcpy(buf, "one");
    strncat(buf, "twothree", 3);
    show("strncat", buf);

    /* ---- the comparisons ---- */
    ok("strcmp equal", strcmp("abc", "abc") == 0);
    ok("strcmp less", strcmp("abc", "abd") < 0);
    ok("strcmp greater", strcmp("abd", "abc") > 0);
    ok("strcmp on a prefix", strcmp("ab", "abc") < 0);
    ok("strcmp compares as unsigned", strcmp("\200", "\177") > 0);
    ok("strncmp stops at n", strncmp("abcX", "abcY", 3) == 0);
    ok("strncmp sees the nth byte", strncmp("abcX", "abcY", 4) < 0);
    ok("strncmp of nothing", strncmp("a", "b", 0) == 0);

    /* Unaligned comparands: the same bytes, one of them mid-word. */
    strcpy(buf, "!hello");
    ok("strcmp across a word boundary", strcmp(buf + 1, "hello") == 0);

    /* ---- the searches ---- */
    p = "abcabc";
    ok("strchr finds the first", strchr(p, 'b') == p + 1);
    ok("strrchr finds the last", strrchr(p, 'b') == p + 4);
    ok("strchr misses", strchr(p, 'z') == 0);
    ok("strchr finds the NUL", strchr(p, 0) == p + 6);
    ok("index is strchr", index(p, 'c') == p + 2);
    ok("rindex is strrchr", rindex(p, 'c') == p + 5);
    ok("rindex misses", rindex(p, 'z') == 0);
    q = "hello world";
    ok("strstr finds a substring", strstr(q, "o w") == q + 4);
    ok("strstr matches to the end", strstr(q, "world") == q + 6);
    ok("strstr misses", strstr(q, "wordl") == 0);
    ok("strstr of the empty needle", strstr(q, "") == q);

    /* ---- strtok, which keeps its own state between calls ---- */
    strcpy(buf, "  a:bb::ccc  ");
    p = strtok(buf, " :");
    show("strtok 1", p);
    p = strtok(0, " :");
    show("strtok 2", p);
    p = strtok(0, " :");
    show("strtok 3", p);
    ok("strtok runs out", strtok(0, " :") == 0);

    /* ---- the mem* family ---- */
    memset(buf, '.', 12);
    buf[12] = 0;
    show("memset", buf);
    memset(buf + 2, '-', 3);
    show("memset unaligned", buf);
    ok("memset returns its destination", memset(b2, 0, 4) == b2);

    strcpy(buf, "abcdefghij");
    memcpy(b2, buf, 11);
    show("memcpy", b2);
    memcpy(b2 + 1, buf, 4);
    show("memcpy unaligned", b2);

    /*
     * The two overlap directions, both INSIDE one word, which is where a raw word
     * comparison of the two pointers would pick the wrong direction and shred the
     * result.  Backward first (dest above source), then forward.
     */
    strcpy(buf, "abcdefghij");
    memmove(buf + 2, buf, 8);
    buf[10] = 0;
    show("memmove backward", buf);
    strcpy(buf, "abcdefghij");
    memmove(buf, buf + 2, 8);
    show("memmove forward", buf);

    ok("memcmp equal", memcmp("abcd", "abcd", 4) == 0);
    ok("memcmp differs", memcmp("abcd", "abce", 4) < 0);
    ok("memcmp stops at n", memcmp("abcd", "abce", 3) == 0);

    /* memcmp must look past a NUL where strcmp would stop. */
    ok("memcmp ignores NUL", memcmp("a\0c", "a\0d", 3) < 0);
    ok("... where strcmp stops", strcmp("a\0c", "a\0d") == 0);
    q = "abcabc";
    ok("memchr finds", (char *)memchr(q, 'c', 6) == q + 2);
    ok("memchr misses within n", memchr(q, 'c', 2) == 0);

    /* ---- swab: adjacent bytes, in place and out of place ---- */
    memcpy(buf, "abcdef", 7);
    swab(buf, b2, 6);
    b2[6] = 0;
    show("swab", b2);
    swab(buf, buf, 6);
    show("swab in place", buf);
    memcpy(buf, "abcde", 6);
    swab(buf, buf, 5);
    show("swab leaves an odd byte", buf);

    put("done\n");
    return 0;
}
