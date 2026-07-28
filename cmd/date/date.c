/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// date -- print, and with an argument set, the date and time.
//
//      date                    print it
//      date yymmddhhmm[.ss]    set it; super-user only
//      date -u ...             work in GMT rather than local time
//
// The first of task C2a's three (../README.md), and the first program on this image that
// changes GLOBAL MACHINE STATE rather than a file: stime(2) moves the clock every other
// process reads.
//
// TWO THINGS HAD TO CHANGE IN THE ARITHMETIC, and neither is a C11 detail.
//
// 1.  stime(2) TAKES THE time_t BY VALUE HERE, not by pointer.  v7's time_t was two
//     PDP-11 words, so its gate took both and its libc stub dereferenced the caller's
//     pointer to push them; here a time_t is one 41-bit word, sysent[25] says narg 1, and
//     lib/libc/sys/ generates a UNIFORM stub that issues the extracode and dereferences
//     nothing (lib/libc/sys/syscalls.tbl).  The kernel's handler reads `struct a { time_t
//     time; }' straight out of u_ap (kernel/sys4.c).  So `stime(&timbuf)' would have set
//     the clock to the bit pattern of a pointer.  doc/Unix_V7_System_Calls.md had it
//     right; lib/libc/man/stime.2 and syscalls.tbl's prototype comment did not, and this
//     is the first caller either has ever had.  Both are corrected.
//
// 2.  dysize() COUNTS YEARS FROM 1900, as struct tm does -- lib/libc/gen/ctime.c adds the
//     1900 itself.  v7's date passed ABSOLUTE years, which was harmless against v7's
//     `(y % 4) == 0' leap rule and is wrong against the full Gregorian one this libc uses:
//     dysize(2000) asks about the year 3900, which is not a leap year, so every date from
//     March 2000 on would have come out a day early.  Both call sites subtract 1900 now.
//
// 3.  AND THE YEAR ITSELF, which is the one change here that a user can see.  v7 wrote
//     `year += 1900' with no window at all, so this program could name no year after 1999
//     -- and `date 0001010000' did not fail, it meant 1 January 1900, seventy years before
//     the epoch, out of which the day count below comes back as a wrong date rather than
//     as an error.  On a machine whose time_t reaches past 2100 and whose only way to set
//     a clock is this program, that is not a curiosity.  Two-digit years 70..99 mean
//     1970..1999 and 00..69 mean 2000..2069, and the pivot is 70 because that is where a
//     time_t begins: every year the window can name is one the machine can hold.  date.1
//     says so, and note that it is also what makes finding 2 REACHABLE -- the old and new
//     leap rules differ only at a century, and no century was expressible before.
//
// THE ONE char * COMPARISON (../README.md §2) is gtime()'s `while (sp < ep)', the loop
// that reverses the argument in place so that the fields can be parsed from the right --
// which is how omitting the leading ones works.  `<' between two char * does not order
// them on this machine: the byte offset lives above the word address and DECREMENTS as
// the pointer advances.  Rewritten as a pair of int indices over the array, as
// ls's makename() and kernel/prim.c were.  gp()'s `*sp++' walking is untouched; it is
// dereference and increment, which are both fine.
//
// /usr/adm/wtmp IS NOT ON THIS IMAGE (root.manifest declares no /usr/adm), so the pair of
// records v7 writes to record a clock change is simply not written -- v7 already guards
// the write with `if ((wf = open(...)) >= 0)', so nothing had to be removed.  It comes
// alive with task C6, which brings login, who and the rest of utmp.  Deliberately NOT
// created here: a file that grows on every clock change would make every image-diffing
// test non-reproducible.
//
// NOT SETUID, and it must not become so: stime(2) is gated on suser() (kernel/sys4.c),
// and that gate is the entire reason an ordinary user cannot move the clock.
//
// THE MACHINE IS ALWAYS GMT.  TIMEZONE and DSTFLAG are both 0 (<sys/param.h>), ftime()
// hands those out unchanged, and timezone(0, 0) is "GMT" -- so `date' and `date -u' print
// the same instant and differ only in where the zone name comes from.  date.1 says so.
//
// The C11 pass is the usual one: prototypes and explicit return types (v7 declared none of
// main, gtime or gp), `static' on the file-scope objects, the ancient `char *ctime();'
// block replaced by <time.h>, and the three routines no header declares -- stime(),
// ftime() and timezone() -- declared here, which is what lib/libc/gen/ctime.c does for the
// last two and what <time.h>'s own header comment says a caller should do.
//
#include <fcntl.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

// Declared by no header: see the note above.
int stime(time_t t);
int ftime(struct timeb *tp);
char *timezone(int zone, int dst);
int dysize(int y);

static time_t timbuf; // v7 wrote `long'; one word here
static char *ap;      // the argument being parsed, then asctime's answer
static char *sp;      // the parse cursor, walking the reversed argument
static int uflag;

static const int dmsize[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// The two wtmp markers v7 writes around a clock change: OLD_TIME then NEW_TIME.
static struct utmp wtmp[2] = { { "|", "", 0 }, { "{", "", 0 } };

//
// One field off the front of the reversed argument, least significant first: two digits,
// or `dfault' if the string has run out, or -1 if what is there is not a number.
//
static int gp(int dfault)
{
    int c, d;

    if (*sp == 0)
        return dfault;
    c = (*sp++) - '0';
    d = (*sp ? (*sp++) - '0' : 0);
    if (c < 0 || c > 9 || d < 0 || d > 9)
        return -1;
    return c + 10 * d;
}

//
// Parse ap as yymmddhhmm[.ss] into timbuf.  Returns 1 if it is not one.
//
// The argument is reversed in place first, so that gp() can take the fields off the end
// -- seconds, minutes, hours, day, month, year -- and each one that is missing simply
// falls back to today's value.  That is v7's trick and it is why the leading fields are
// the optional ones.
//
static int gtime(void)
{
    int i, j, year, month;
    int day, hour, mins, secs;
    struct tm *L;
    char x;

    // Reverse ap in place.  v7 walked two char * cursors towards each other and compared
    // them with `<', which does not order two pointers here (../README.md §2).
    for (j = 0; ap[j]; j++)
        ;
    for (i = 0, j--; i < j; i++, j--) {
        x     = ap[i];
        ap[i] = ap[j];
        ap[j] = x;
    }

    sp = ap;
    time(&timbuf);
    L    = localtime(&timbuf);
    secs = gp(-1);
    if (*sp != '.') {
        mins = secs;
        secs = 0;
    } else {
        sp++;
        mins = gp(-1);
    }
    hour  = gp(-1);
    day   = gp(L->tm_mday);
    month = gp(L->tm_mon + 1);
    year  = gp(L->tm_year);
    if (*sp)
        return 1;
    if (month < 1 || month > 12 || day < 1 || day > 31 || mins < 0 || mins > 59 || secs < 0 ||
        secs > 59)
        return 1;
    if (hour == 24) {
        hour = 0;
        day++;
    }
    if (hour < 0 || hour > 23)
        return 1;

    timbuf = 0;
    // THE TWO-DIGIT YEAR WINDOW, which v7 had not: it wrote `year += 1900' flat, so no
    // year after 1999 could be named at all and `date 0001010000' meant 1900 -- a time
    // before the epoch, out of which the arithmetic below produces nonsense rather than an
    // error.  The pivot is 70 because that is where this machine's time_t begins: every
    // year the window can now name, 1970 through 2069, is one a time_t can hold.  See the
    // head of this file.  gp() returns L->tm_year for an omitted field, which is already
    // years-since-1900 and is never below 70, so the default path is untouched.
    if (year < 70)
        year += 2000;
    else
        year += 1900;
    // dysize() takes years since 1900, as struct tm does; v7 passed absolute ones.
    for (i = 1970; i < year; i++)
        timbuf += dysize(i - 1900);
    /* Leap year */
    if (dysize(year - 1900) == 366 && month >= 3)
        timbuf++;
    while (--month)
        timbuf += dmsize[month - 1];
    timbuf += day - 1;
    timbuf = 24 * timbuf + hour;
    timbuf = 60 * timbuf + mins;
    timbuf = 60 * timbuf + secs;
    return 0;
}

int main(int argc, char *argv[])
{
    char *tzn;
    struct timeb info;
    int wf, rc;

    rc = 0;
    ftime(&info);
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'u') {
        argc--;
        argv++;
        uflag++;
    }
    if (argc > 1) {
        ap = argv[1];
        if (gtime()) {
            printf("date: bad conversion\n");
            return 1;
        }
        /* convert to GMT assuming local time */
        if (uflag == 0) {
            timbuf += info.timezone * 60;
            /* now fix up local daylight time */
            if (localtime(&timbuf)->tm_isdst)
                timbuf -= 60 * 60;
        }
        time(&wtmp[0].ut_time);
        if (stime(timbuf) < 0) {
            rc++;
            printf("date: no permission\n");
        } else if ((wf = open("/usr/adm/wtmp", 1)) >= 0) {
            // Not reached on this image: there is no /usr/adm yet.  See the head of
            // this file.
            time(&wtmp[1].ut_time);
            lseek(wf, 0, 2);
            write(wf, (char *)wtmp, sizeof(wtmp));
            close(wf);
        }
    }
    if (rc == 0)
        time(&timbuf);
    if (uflag) {
        ap  = asctime(gmtime(&timbuf));
        tzn = "GMT";
    } else {
        struct tm *tp;
        tp  = localtime(&timbuf);
        ap  = asctime(tp);
        tzn = timezone(info.timezone, tp->tm_isdst);
    }
    // asctime's answer is "Thu Jan  1 00:00:00 1970\n"; the zone name goes between the
    // seconds and the year, which is what the 20/19 split is doing.
    printf("%.20s", ap);
    if (tzn)
        printf("%s", tzn);
    printf("%s", ap + 19);
    return rc;
}
