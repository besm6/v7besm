/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * ctime, localtime, gmtime, asctime, dysize -- seconds since the epoch to a date.
 *
 * One file, as in v7, because they share the two statics they hand back: the struct tm
 * that gmtime() fills and the 26-character buffer asctime() prints into.  Anything
 * that split them would still have to share those.
 *
 * time_t IS ONE WORD, so the `long *' of every v7 signature is a `const time_t *' here
 * and nothing is passed by halves.  Forty-one signed bits of seconds reach roughly
 * year 36000, which is a good deal further than v7 ever had to answer for -- hence the
 * two places below where v7's arithmetic was not quite general enough.
 *
 * The zone still comes from ftime(), as it did in v7: the kernel owns the offset and
 * the daylight-saving flag, and there is no /usr/lib/zoneinfo to read.  Under b6sim
 * ftime answers zone 0, DST 0, so localtime() and gmtime() agree there.
 *
 * Three departures from v7, each because the original was written for a machine this
 * is not:
 *
 *   dysize() USES THE FULL GREGORIAN RULE.  v7 wrote `(y % 4) == 0' and was right
 *   until 2100, which a two-digit year could not reach and a 41-bit time_t can.
 *
 *   gmtime() AND asctime() NAME THEIR FIELDS.  v7 walked struct tm through an `int *'
 *   -- `tp = (int *)&xtime; *tp++ = ...' -- which works here only because every field
 *   is one word, and says nothing about which field is being written.  The fields are
 *   named instead; the generated code is the same.
 *
 *   FEBRUARY IS NOT PATCHED IN PLACE.  v7 wrote 29 into its static dmsize[1] for the
 *   duration of a leap year and wrote 28 back afterwards.  The length is computed in
 *   the loop instead, so the table stays constant and gmtime() has one less way to
 *   leave the library in a state its caller did not ask for.
 */
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>

int ftime(struct timeb *tp);

static char cbuf[26];

static const int dmsize[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*
 * Used for 1974 and 1975 alone: the day number of the first day after the Sunday of
 * the change, in the two years when the United States moved daylight time around.
 * v7 carried the table and so does this.
 */
static const struct {
    int daylb;
    int dayle;
} daytab[] = {
    { 5, 333 },  /* 1974: Jan 6 - last Sun. in Nov       */
    { 58, 303 }, /* 1975: last Sun. in Feb - last Sun in Oct */
};

/*
 * Days in the year `y', counted from 1900 as struct tm counts it.  A year is a leap
 * year when it divides by four, except centuries, except centuries dividing by four.
 */
int dysize(int y)
{
    y += 1900;
    if (y % 4 != 0)
        return 365;
    if (y % 100 != 0)
        return 366;
    if (y % 400 != 0)
        return 365;
    return 366;
}

/*
 * The argument is a 0-origin day number; the value is the day number of the first
 * Sunday on or after it.  The +700 is v7's, and is there so the remainder is taken of
 * a number that cannot have gone negative.
 */
static int sunday(const struct tm *t, int d)
{
    if (d >= 58)
        d += dysize(t->tm_year) - 365;
    return d - (d - t->tm_yday + t->tm_wday + 700) % 7;
}

struct tm *gmtime(const time_t *clk)
{
    static struct tm xtime;
    time_t hms, day;
    int d0, d1, len;

    /* Split into a day number and the seconds within the day, flooring both. */
    hms = *clk % 86400;
    day = *clk / 86400;
    if (hms < 0) {
        hms += 86400;
        day -= 1;
    }

    xtime.tm_sec  = hms % 60;
    xtime.tm_min  = (hms / 60) % 60;
    xtime.tm_hour = hms / 3600;

    /* 1 January 1970 was a Thursday; the addend is 4 modulo 7. */
    xtime.tm_wday = (day + 7340036) % 7;

    if (day >= 0) {
        for (d1 = 70; day >= dysize(d1); d1++)
            day -= dysize(d1);
    } else {
        for (d1 = 70; day < 0; d1--)
            day += dysize(d1 - 1);
    }
    xtime.tm_year = d1;
    xtime.tm_yday = d0 = day;

    for (d1 = 0;; d1++) {
        len = dmsize[d1];
        if (d1 == 1 && dysize(xtime.tm_year) == 366)
            len = 29;
        if (d0 < len)
            break;
        d0 -= len;
    }
    xtime.tm_mday  = d0 + 1;
    xtime.tm_mon   = d1;
    xtime.tm_isdst = 0;
    return &xtime;
}

struct tm *localtime(const time_t *clk)
{
    struct tm *ct;
    struct timeb systime;
    time_t copyt;
    int dayno, daylbegin, daylend;

    ftime(&systime);
    copyt = *clk - (time_t)systime.timezone * 60;
    ct    = gmtime(&copyt);

    dayno     = ct->tm_yday;
    daylbegin = 119; /* last Sun in Apr */
    daylend   = 303; /* last Sun in Oct */
    if (ct->tm_year == 74 || ct->tm_year == 75) {
        daylbegin = daytab[ct->tm_year - 74].daylb;
        daylend   = daytab[ct->tm_year - 74].dayle;
    }
    daylbegin = sunday(ct, daylbegin);
    daylend   = sunday(ct, daylend);
    if (systime.dstflag && (dayno > daylbegin || (dayno == daylbegin && ct->tm_hour >= 2)) &&
        (dayno < daylend || (dayno == daylend && ct->tm_hour < 1))) {
        copyt += 1 * 60 * 60;
        ct = gmtime(&copyt);
        ct->tm_isdst++;
    }
    return ct;
}

/*
 * Two decimal digits at cp+1, the tens blanked when n is below ten -- which is what
 * makes the day of the month space-padded and the clock zero-padded, the caller adding
 * 100 to whatever it wants padded with a zero.  v7's trick, kept.
 */
static char *ct_numb(char *cp, int n)
{
    cp++;
    if (n >= 10)
        *cp++ = (n / 10) % 10 + '0';
    else
        *cp++ = ' ';
    *cp++ = n % 10 + '0';
    return cp;
}

char *asctime(const struct tm *t)
{
    const char *ncp;
    char *cp;
    int year;

    cp = cbuf;
    for (ncp = "Day Mon 00 00:00:00 1900\n"; (*cp++ = *ncp++) != 0;)
        ;

    ncp   = &"SunMonTueWedThuFriSat"[3 * t->tm_wday];
    cp    = cbuf;
    *cp++ = *ncp++;
    *cp++ = *ncp++;
    *cp++ = *ncp++;
    cp++;

    ncp   = &"JanFebMarAprMayJunJulAugSepOctNovDec"[3 * t->tm_mon];
    *cp++ = *ncp++;
    *cp++ = *ncp++;
    *cp++ = *ncp++;

    cp = ct_numb(cp, t->tm_mday);
    cp = ct_numb(cp, t->tm_hour + 100);
    cp = ct_numb(cp, t->tm_min + 100);
    cp = ct_numb(cp, t->tm_sec + 100);

    /*
     * All four digits of the year, where v7 wrote the last two into a literal "19"
     * and patched a "20" over it above 1999.  A 41-bit time_t reaches years that
     * neither spelling covers, so a year outside 0..9999 -- which C11 leaves
     * undefined anyway -- is simply reported modulo 10000 rather than overrunning.
     */
    year = t->tm_year + 1900;
    if (year < 0)
        year = -year;
    year %= 10000;
    cp++;
    *cp++ = year / 1000 + '0';
    *cp++ = (year / 100) % 10 + '0';
    *cp++ = (year / 10) % 10 + '0';
    *cp++ = year % 10 + '0';
    return cbuf;
}

char *ctime(const time_t *clk)
{
    return asctime(localtime(clk));
}
