// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// /usr/lib/calendar produces an egrep -f file
// that will select today's and tomorrow's
// calendar entries, with special weekend provisions
//
// used by calendar command
#include <stdio.h>
#include <time.h>

#define DAY (3600 * 24) // a long is one word here, so v7's L is noise

static char *month[] = {
    "[Jj]an",
    "[Ff]eb",
    "[Mm]ar",
    "[Aa]pr",
    "[Mm]ay",
    "[Jj]un",
    "[Jj]ul",
    "[Aa]ug",
    "[Ss]ep",
    "[Oo]ct",
    "[Nn]ov",
    "[Dd]ec",
};

static void tprint(time_t t)
{
    struct tm *tm;

    tm = localtime(&t);
    // 0* before the month is not v7's `%d/'; README.md.
    printf("(^|[ (,;])((%s[^ ]* *|0*%d/)0*%d)([^0123456789]|$)\n", month[tm->tm_mon], tm->tm_mon + 1,
           tm->tm_mday);
}

int main(void)
{
    time_t t;

    time(&t);
    tprint(t);
    switch (localtime(&t)->tm_wday) {
    case 5:
        t += DAY;
        tprint(t);
        // fall through
    case 6:
        t += DAY;
        tprint(t);
        // fall through
    default:
        t += DAY;
        tprint(t);
    }
    return 0;
}
