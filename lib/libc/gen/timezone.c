/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * timezone(zone, dst) -- the name of the zone `zone' minutes west of Greenwich.
 *
 * v7's, table and apology intact: it knows six zones and manufactures a "GMT+h:mm" for
 * everything else.  ftime() is where a caller gets the two arguments; nothing here
 * calls it, which is why this is a file of its own and not part of gen/ctime.c.
 *
 * No header declares it -- v7 had none and C11 has strftime's %Z instead -- so a caller
 * declares it itself, exactly as one declares index() or isatty().
 */
#include <stdio.h>

static const struct zone {
    int offset;
    const char *stdzone;
    const char *dlzone;
} zonetab[] = {
    { 4 * 60, "AST", "ADT" }, /* Atlantic */
    { 5 * 60, "EST", "EDT" }, /* Eastern  */
    { 6 * 60, "CST", "CDT" }, /* Central  */
    { 7 * 60, "MST", "MDT" }, /* Mountain */
    { 8 * 60, "PST", "PDT" }, /* Pacific  */
    { 0, "GMT", 0 },          /* Greenwich */
    { -1, 0, 0 },
};

char *timezone(int zone, int dst)
{
    const struct zone *zp;
    static char czone[10];
    const char *sign;

    for (zp = zonetab; zp->offset != -1; zp++) {
        if (zp->offset == zone) {
            if (dst && zp->dlzone)
                return (char *)zp->dlzone;
            if (!dst && zp->stdzone)
                return (char *)zp->stdzone;
        }
    }
    if (zone < 0) {
        zone = -zone;
        sign = "+";
    } else {
        sign = "-";
    }
    sprintf(czone, "GMT%s%d:%02d", sign, zone / 60, zone % 60);
    return czone;
}
