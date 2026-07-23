/*
 * strftime(s, max, fmt, tp) -- a broken-down time as text (C11 §7.27.3.5).
 *
 * No v7 ancestor: v7 had asctime() and nothing else, and the whole of §7.27.3.5 is
 * written here from the standard.  The locale is "C", which is the only locale this
 * system has (<locale.h> offers setlocale and one name), so the E and O modifiers are
 * accepted and ignored -- §7.27.3.5p4 says an unsupported modifier is treated as the
 * unmodified specifier, and in the C locale every alternative form IS the normal one.
 *
 * The compound specifiers -- %c %D %F %r %R %T %x %X -- are expanded by calling the
 * walker on their definition, so each is written once and reads as the standard states
 * it.  The recursion goes exactly one level deep: no expansion contains another.
 *
 * THE RETURN VALUE COUNTS ONLY A COMPLETE RESULT.  §7.27.3.5p3: if the total including
 * the terminating null would exceed maxsize, zero comes back and the array's contents
 * are indeterminate -- so nothing is written past the end and, when maxsize is zero,
 * nothing is written at all, terminator included.
 *
 * %Z and %z ask the system where it is, and the answer comes from ftime(), as
 * gen/ctime.c's localtime() takes it; %Z spells it through timezone(), which is what
 * knows the six names v7 knew.  That is the one heavy reference in this file --
 * timezone() formats the fallback with sprintf, so a program calling strftime links
 * the printf engine whether it prints anything or not.
 */
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>

int ftime(struct timeb *tp);
char *timezone(int zone, int dst);

static const char *wday_name[7] = { "Sunday",   "Monday", "Tuesday", "Wednesday",
                                    "Thursday", "Friday", "Saturday" };

static const char *mon_name[12] = { "January",   "February", "March",    "April",
                                    "May",       "June",     "July",     "August",
                                    "September", "October",  "November", "December" };

/*
 * The sink.  `over' latches as soon as one character would not fit, and the walk runs
 * on to the end regardless: the count is discarded either way, and stopping early
 * would only make the failure depend on where in the format it happened.
 */
struct sink {
    char *s;
    size_t max;
    size_t n;
    int over;
};

static void put(struct sink *k, int c)
{
    if (k->n + 1 >= k->max) {
        k->over = 1;
        return;
    }
    k->s[k->n++] = c;
}

static void puts_(struct sink *k, const char *s)
{
    while (*s)
        put(k, *s++);
}

/* n right-adjusted in `width' columns, padded with `pad'; width 0 means as it comes. */
static void putnum(struct sink *k, int n, int width, int pad)
{
    char buf[16]; /* 41 signed bits is 13 digits, a sign, and slack */
    int i = 0, neg = 0;

    if (n < 0) {
        neg = 1;
        n   = -n;
    }
    do {
        buf[i++] = n % 10 + '0';
        n /= 10;
    } while (n != 0);
    if (neg)
        buf[i++] = '-';
    while (i < width)
        buf[i++] = pad;
    while (i > 0)
        put(k, buf[--i]);
}

static int isleap(int fullyear)
{
    if (fullyear % 4 != 0)
        return 0;
    if (fullyear % 100 != 0)
        return 1;
    return fullyear % 400 == 0;
}

/*
 * ISO 8601 has 53 weeks in a year whose 1 January is a Thursday, and in a leap year
 * whose 1 January is a Wednesday.  jan1() gives that weekday with Sunday counted 0,
 * so Thursday is 4: 1 January 1970 comes out 4, which it was.
 */
static int jan1(int fullyear)
{
    int y = fullyear - 1;

    return (1 + y + y / 4 - y / 100 + y / 400) % 7; /* 0 = Sunday */
}

static int weeks_in(int fullyear)
{
    int d = jan1(fullyear);

    if (d == 4 || (d == 3 && isleap(fullyear)))
        return 53;
    return 52;
}

/*
 * The ISO 8601 week number, and through *iyear the week-based year it belongs to --
 * which is the calendar year of the week's Thursday, so the first days of January can
 * fall in the last week of the year before.
 */
static int isoweek(const struct tm *t, int *iyear)
{
    int wday = (t->tm_wday + 6) % 7; /* Monday = 0 */
    int year = t->tm_year + 1900;
    int week = (t->tm_yday - wday + 10) / 7;

    if (week < 1) {
        year--;
        week = weeks_in(year);
    } else if (week > weeks_in(year)) {
        year++;
        week = 1;
    }
    *iyear = year;
    return week;
}

static void walk(struct sink *k, const char *fmt, const struct tm *t);

static void conv(struct sink *k, int c, const struct tm *t)
{
    struct timeb systime;
    int i, west;

    switch (c) {
    case 'a':
        for (i = 0; i < 3; i++)
            put(k, wday_name[t->tm_wday % 7][i]);
        return;
    case 'A':
        puts_(k, wday_name[t->tm_wday % 7]);
        return;
    case 'b':
    case 'h':
        for (i = 0; i < 3; i++)
            put(k, mon_name[t->tm_mon % 12][i]);
        return;
    case 'B':
        puts_(k, mon_name[t->tm_mon % 12]);
        return;
    case 'c':
        walk(k, "%a %b %e %H:%M:%S %Y", t);
        return;
    case 'C':
        putnum(k, (t->tm_year + 1900) / 100, 2, '0');
        return;
    case 'd':
        putnum(k, t->tm_mday, 2, '0');
        return;
    case 'D':
        walk(k, "%m/%d/%y", t);
        return;
    case 'e':
        putnum(k, t->tm_mday, 2, ' ');
        return;
    case 'F':
        walk(k, "%Y-%m-%d", t);
        return;
    case 'g':
        i = 0;
        isoweek(t, &i);
        putnum(k, i % 100, 2, '0');
        return;
    case 'G':
        i = 0;
        isoweek(t, &i);
        putnum(k, i, 0, '0');
        return;
    case 'H':
        putnum(k, t->tm_hour, 2, '0');
        return;
    case 'I':
        i = t->tm_hour % 12;
        putnum(k, i == 0 ? 12 : i, 2, '0');
        return;
    case 'j':
        putnum(k, t->tm_yday + 1, 3, '0');
        return;
    case 'm':
        putnum(k, t->tm_mon + 1, 2, '0');
        return;
    case 'M':
        putnum(k, t->tm_min, 2, '0');
        return;
    case 'n':
        put(k, '\n');
        return;
    case 'p':
        puts_(k, t->tm_hour < 12 ? "AM" : "PM");
        return;
    case 'r':
        walk(k, "%I:%M:%S %p", t);
        return;
    case 'R':
        walk(k, "%H:%M", t);
        return;
    case 'S':
        putnum(k, t->tm_sec, 2, '0');
        return;
    case 't':
        put(k, '\t');
        return;
    case 'T':
    case 'X':
        walk(k, "%H:%M:%S", t);
        return;
    case 'u':
        putnum(k, t->tm_wday == 0 ? 7 : t->tm_wday, 0, '0');
        return;
    case 'U':
        putnum(k, (t->tm_yday + 7 - t->tm_wday) / 7, 2, '0');
        return;
    case 'V':
        putnum(k, isoweek(t, &i), 2, '0');
        return;
    case 'w':
        putnum(k, t->tm_wday, 0, '0');
        return;
    case 'W':
        putnum(k, (t->tm_yday + 7 - (t->tm_wday + 6) % 7) / 7, 2, '0');
        return;
    case 'x':
        walk(k, "%m/%d/%y", t);
        return;
    case 'y':
        i = (t->tm_year + 1900) % 100;
        putnum(k, i < 0 ? -i : i, 2, '0');
        return;
    case 'Y':
        putnum(k, t->tm_year + 1900, 0, '0');
        return;
    case 'z':
        /*
         * ISO 8601 counts east of Greenwich; ftime's timezone counts west, in
         * minutes, and does not itself know whether daylight time is in effect --
         * localtime() adds the hour separately, so this does too.
         */
        ftime(&systime);
        west = systime.timezone - (t->tm_isdst > 0 ? 60 : 0);
        put(k, west > 0 ? '-' : '+');
        if (west < 0)
            west = -west;
        putnum(k, west / 60, 2, '0');
        putnum(k, west % 60, 2, '0');
        return;
    case 'Z':
        ftime(&systime);
        puts_(k, timezone(systime.timezone, t->tm_isdst > 0));
        return;
    case '%':
        put(k, '%');
        return;
    default:
        /* Not a specifier: §7.27.3.5 leaves it undefined, so pass it through. */
        put(k, '%');
        put(k, c);
        return;
    }
}

static void walk(struct sink *k, const char *fmt, const struct tm *t)
{
    int c;

    while ((c = *fmt++) != '\0') {
        if (c != '%') {
            put(k, c);
            continue;
        }
        c = *fmt++;
        if (c == 'E' || c == 'O') /* C locale: no alternative forms */
            c = *fmt++;
        if (c == '\0')
            break;
        conv(k, c, t);
    }
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tp)
{
    struct sink k;

    k.s    = s;
    k.max  = max;
    k.n    = 0;
    k.over = 0;
    walk(&k, fmt, tp);
    if (k.over || k.n >= max)
        return 0;
    s[k.n] = '\0';
    return k.n;
}
