//
// mktime(tp) -- a broken-down local time back to a time_t (C11 §7.27.2.3).
//
// No v7 ancestor; v7 converted in one direction only.  This is the inverse of
// gen/ctime.c's localtime(), and it has to agree with it exactly, so it takes its zone
// from the same place -- ftime() -- and applies the same one-hour daylight offset.
//
// §7.27.2.3 asks for three things beyond the conversion: the members of *tp may be
// outside their normal ranges on the way in, tm_wday and tm_yday are ignored on the way
// in and set on the way out, and tm_isdst carries the caller's answer to "is daylight
// time in effect?" -- positive yes, zero no, negative "work it out".  The last is done
// the way every implementation does it: convert once assuming standard time, ask
// localtime() what it makes of the result, and if it says daylight, convert again.
//
// Normalizing is left to localtime() at the end rather than done field by field here:
// every out-of-range field is folded into the seconds count on the way in -- an hour of
// 30 is simply 30*3600 seconds -- so the time_t is right whatever the caller wrote, and
// a localtime() of it fills the whole struct back in with the canonical values.  Only
// the month has to be normalized by hand, since it selects a row of the table below
// rather than scaling anything.
//
// The month table is a copy of gen/ctime.c's, twelve words of it, rather than a shared
// symbol: one function per file is what lets b6ranlib's index leave this object out of
// a program that never calls mktime.
//
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>

int ftime(struct timeb *tp);
int dysize(int y);

static const int dmsize[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

//
// Days from 1 January 1970 to the first of `mon' in `year', where the year is counted
// from 1900 as struct tm counts it.  Negative before the epoch.
//
static int daysto(int year, int mon)
{
    int d = 0, i;

    if (year >= 70) {
        for (i = 70; i < year; i++)
            d += dysize(i);
    } else {
        for (i = year; i < 70; i++)
            d -= dysize(i);
    }
    for (i = 0; i < mon; i++) {
        d += dmsize[i];
        if (i == 1 && dysize(year) == 366)
            d++;
    }
    return d;
}

time_t mktime(struct tm *tp)
{
    struct timeb systime;
    struct tm *ct;
    time_t t;
    int year, mon;

    // The one field that must be in range before it can index the table.
    mon  = tp->tm_mon;
    year = tp->tm_year + mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        year--;
    }

    t = (time_t)daysto(year, mon) + (tp->tm_mday - 1);
    t = t * 86400 + (time_t)tp->tm_hour * 3600 + (time_t)tp->tm_min * 60 + tp->tm_sec;

    // Local to universal: localtime() subtracts the offset, so this adds it.
    ftime(&systime);
    t += (time_t)systime.timezone * 60;

    if (tp->tm_isdst < 0) {
        ct = localtime(&t);
        if (ct->tm_isdst > 0)
            t -= 3600;
    } else if (tp->tm_isdst > 0) {
        t -= 3600;
    }

    // Hand back the canonical fields, tm_wday and tm_yday included.
    *tp = *localtime(&t);
    return t;
}
