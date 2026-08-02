/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// cal -- print a calendar.
//
//      cal [ month ] year
//
// One of task C5f's seven (../TODO.md), and the cleanest of them: no §2, no §3, no §4, no
// §11 and no floating point anywhere.  It is worth saying why, because it is not luck.
//
// EVERY INTERMEDIATE WAS ALREADY BOUNDED.  jan1() computes `4 + y + (y+3)/4' and then
// subtracts centuries, and main() clamps the year to 1..9999 before either runs, so the
// largest number this program ever forms is about 12,500.  It fitted a PDP-11 int by design
// and it fits a 41-bit one without a thought.  The largest constant in the file is 6*72.
//
// Three things were fixed rather than carried, and one was a wild access:
//
//   pstr() WALKED OFF BOTH ENDS OF ITS ROW.  It scanned back over trailing blanks with
//   `i = n+1; while (i--) if (*--s != ' ') break;', which on an all-blank row decrements one
//   step past the start and reads str[-1]; and it then wrote `s[1] = '\0'', which for a row
//   whose last column is full lands one byte past the row.  For the three-month layout that
//   is string[432] of a 432-byte array.  It is an index pair now and the array carries the
//   terminator.
//
//   cal() MUTATED A FILE-SCOPE TABLE.  v7 wrote 28, 29, 19 and 30 into its static mon[] for
//   the duration of a call and left `mon[m] += 11' behind after September 1752.  It happens
//   to be harmless -- every call reassigns mon[2] and mon[9] before reading them -- but the
//   reasoning is not in the code and the next reader has to redo it.  The table is const now
//   and cal() takes a copy.
//
//   `%u' FOR A YEAR.  ../README.md §3: prefer int wherever v7 wrote unsigned for no reason.
//
// THE 1752 CHANGEOVER IS v7's AND IS LEFT EXACTLY AS IT STANDS, including the switch on
// `(jan1(y+1)+7-jan1(y))%7' whose `default:' arm is the year Britain dropped eleven days.
// So `cal 9 1752' still prints 1, 2, 14, 15 ... and `cal 1752' still prints a September with
// nineteen days in it.  It is the one output of this program everybody checks.
//
// NOT SETUID: it opens nothing at all.
//
#include <stdio.h>
#include <stdlib.h>

#define CALW 72 // the three-month row is 72 columns; a single month is 24 of them
#define ROWS 6  // no month spans more than six weeks

static const char dayw[] = " S  M Tu  W Th  F  S";

static const char *const smon[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December",
};

// Days per month with February and September at their long values; cal() shortens whichever
// of the two the year calls for.  Index 0 is unused so that a month number indexes directly.
static const int monlen[13] = {
    0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

// One trailing byte for pstr()'s terminator: v7's row was exactly ROWS*CALW and pstr() wrote
// past the last one.
static char string[ROWS * CALW + 1];

static int number(const char *str);
static void pstr(char *str, int n);
static void cal(int m, int y, char *p, int w);
static int jan1(int yr);

int main(int argc, char **argv)
{
    int y, i, j;
    int m;

    if (argc < 2) {
        printf("usage: cal [month] year\n");
        exit(0);
    }
    if (argc == 2)
        goto xlong;

    //
    //	print out just month
    //

    m = number(argv[1]);
    if (m < 1 || m > 12)
        goto badarg;
    y = number(argv[2]);
    if (y < 1 || y > 9999)
        goto badarg;
    printf("   %s %d\n", smon[m - 1], y);
    printf("%s\n", dayw);
    for (i = 0; i < ROWS * 24; i++)
        string[i] = '\0';
    cal(m, y, string, 24);
    for (i = 0; i < ROWS * 24; i += 24)
        pstr(string + i, 24);
    exit(0);

    //
    //	print out complete year
    //

xlong:
    y = number(argv[1]);
    if (y < 1 || y > 9999)
        goto badarg;
    printf("\n\n\n");
    printf("				%d\n", y);
    printf("\n");
    for (i = 0; i < 12; i += 3) {
        for (j = 0; j < ROWS * CALW; j++)
            string[j] = '\0';
        printf("	 %.3s", smon[i]);
        printf("			%.3s", smon[i + 1]);
        printf("		       %.3s\n", smon[i + 2]);
        printf("%s   %s   %s\n", dayw, dayw, dayw);
        cal(i + 1, y, string, CALW);
        cal(i + 2, y, string + 23, CALW);
        cal(i + 3, y, string + 46, CALW);
        for (j = 0; j < ROWS * CALW; j += CALW)
            pstr(string + j, CALW);
    }
    printf("\n\n\n");
    exit(0);

badarg:
    printf("Bad argument\n");
    return 0;
}

static int number(const char *str)
{
    int n, c;
    const char *s;

    n = 0;
    s = str;
    while ((c = *s++) != '\0') {
        if (c < '0' || c > '9')
            return 0;
        n = n * 10 + c - '0';
    }
    return n;
}

//
// Print one row of the layout: NULs become blanks, trailing blanks are cut, and the row is
// terminated.  v7 walked this with a pointer and stepped one place off each end.
//
static void pstr(char *str, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (str[i] == '\0')
            str[i] = ' ';
    for (i = n; i > 0 && str[i - 1] == ' '; i--)
        ;
    str[i] = '\0';
    printf("%s\n", str);
}

//
// Lay month m of year y into the six rows starting at p, each w columns wide.
//
static void cal(int m, int y, char *p, int w)
{
    int mon[13];
    int d, i;
    char *s;

    for (i = 0; i <= 12; i++)
        mon[i] = monlen[i];

    s = p;
    d = jan1(y);

    switch ((jan1(y + 1) + 7 - d) % 7) {

    //
    //	non-leap year
    //
    case 1:
        mon[2] = 28;
        break;

    //
    //	1752
    //
    default:
        mon[9] = 19;
        break;

    //
    //	leap year
    //
    case 2:;
    }
    for (i = 1; i < m; i++)
        d += mon[i];
    d %= 7;
    s += 3 * d;
    for (i = 1; i <= mon[m]; i++) {
        if (i == 3 && mon[m] == 19) {
            i += 11;
            mon[m] += 11;
        }
        if (i > 9)
            *s = i / 10 + '0';
        s++;
        *s++ = i % 10 + '0';
        s++;
        if (++d == 7) {
            d = 0;
            s = p + w;
            p = s;
        }
    }
}

//
//	return day of the week
//	of jan 1 of given year
//
static int jan1(int yr)
{
    int y, d;

    //
    //	normal gregorian calendar
    //	one extra day per four years
    //

    y = yr;
    d = 4 + y + (y + 3) / 4;

    //
    //	julian calendar
    //	regular gregorian
    //	less three days per 400
    //

    if (y > 1800) {
        d -= (y - 1701) / 100;
        d += (y - 1601) / 400;
    }

    //
    //	great calendar changeover instant
    //

    if (y > 1752)
        d += 3;

    return d % 7;
}
