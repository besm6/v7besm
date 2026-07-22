// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#ifndef _UTMP_H
#define _UTMP_H

struct utmp {
    char ut_line[8]; // tty name
    char ut_name[8]; // user id
    long ut_time;    // time on
};

#endif // _UTMP_H
