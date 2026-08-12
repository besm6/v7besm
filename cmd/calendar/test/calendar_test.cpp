//
// Unit tests for the cmd/calendar shell script and the generator it runs.
//
// /usr/bin/calendar is a shell script; /usr/lib/calendar is the C program that
// prints the egrep pattern for today and tomorrow.  The script is driven here
// under the host shell with LIBCAL pointing at a wrapper that runs the REAL
// target generator under b6sim, so what is asserted is the staged binary's own
// output travelling through the staged script.
//
// Every fixture holds every date of the year, because the answer moves daily:
// the test works out which lines must come back and which must not.
//
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char MARK[] = "MARK";

// A private directory for one test, under the build tree.
std::string workdir(const std::string &name)
{
    std::string dir = "calendar_" + name;
    std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "'";
    EXPECT_EQ(std::system(cmd.c_str()), 0) << cmd;
    return dir;
}

void writefile(const std::string &path, const std::string &text)
{
    std::ofstream f(path, std::ios::trunc);
    f << text;
}

// A file with one zero-padded entry per date of the year -- "01/06\tMARK 1/6".
// Zero-padded on purpose: v7's pattern tolerated a leading zero on the day and
// not on the month, which is the deviation this port made and this asserts.
std::string everydate()
{
    std::string s;
    char buf[64];
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= 31; d++) {
            std::snprintf(buf, sizeof(buf), "%02d/%02d\t%s %d/%d\n", m, d, MARK, m, d);
            s += buf;
        }
    return s;
}

// The wrapper the script execs as /usr/lib/calendar.
std::string generator(const std::string &dir)
{
    std::string path = dir + "/gen";
    writefile(path, std::string("#!/bin/sh\nexec '") + B6SIM_EXE "' '" LIBCAL_EXE "'\n");
    EXPECT_EQ(chmod(path.c_str(), 0755), 0);
    return "gen";
}

// Run the shipped script in `dir` with the given argument, and return its
// standard output.  `caldir' is relative to `dir', the script being run from
// there.  Standard error is dropped: b6sim and the host egrep both have things
// to say that are not the subject here.
std::string run(const std::string &dir, const std::string &arg, const std::string &caldir)
{
    std::string cmd = "cd '" + dir + "' && LIBCAL=./gen CALDIR='" + caldir + "' /bin/sh '" +
                      CALENDAR_SH + "' " + arg + " 2>/dev/null";

    FILE *p = popen(cmd.c_str(), "r");
    EXPECT_NE(p, nullptr) << cmd;
    if (!p)
        return {};

    std::string out;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
        out.append(buf, n);
    pclose(p);
    return out;
}

// GMT AND NOT LOCAL TIME: b6sim's ftime answers zone 0 and DST 0, so the
// generator's localtime() is gmtime() there (lib/libc/gen/ctime.c).
std::string offsetday(int days)
{
    time_t t = std::time(nullptr) + days * 24 * 3600;
    const struct tm *tm = std::gmtime(&t);
    return std::to_string(tm->tm_mon + 1) + "/" + std::to_string(tm->tm_mday);
}

int today_wday()
{
    time_t t = std::time(nullptr);
    return std::gmtime(&t)->tm_wday;
}

// How many days the generator covers: today plus one, except that Friday reaches
// Monday and Saturday reaches Monday too.
int span()
{
    switch (today_wday()) {
    case 5:
        return 4;
    case 6:
        return 3;
    default:
        return 2;
    }
}

int lines(const std::string &out)
{
    return static_cast<int>(std::count(out.begin(), out.end(), '\n'));
}

bool has(const std::string &out, const std::string &needle)
{
    return out.find(needle) != std::string::npos;
}

} // namespace

// The whole pipeline over a ./calendar in the current directory: the target
// generator's pattern, the host egrep, and one line per day it covers.  A
// zero-padded month is the point -- with v7's `%d/' every line here would be
// missed for eleven months of the year.
TEST(Calendar, LocalCalendarEveryDate)
{
    std::string dir = workdir("local");
    generator(dir);
    writefile(dir + "/calendar", everydate());

    std::string out = run(dir, "", "none");

    EXPECT_EQ(lines(out), span()) << out;
    for (int i = 0; i < span(); i++)
        EXPECT_TRUE(has(out, std::string(MARK) + " " + offsetday(i) + "\n")) << out;
    EXPECT_FALSE(has(out, std::string(MARK) + " " + offsetday(span()) + "\n")) << out;
}

// With no ./calendar the system database answers instead, and egrep -h keeps the
// file names out of it.  Two files, so the -h would show if it were missing.
TEST(Calendar, SystemDatabaseFallback)
{
    std::string dir = workdir("fallback");
    generator(dir);

    std::string db = dir + "/db";
    EXPECT_EQ(std::system(("mkdir -p '" + db + "'").c_str()), 0);
    writefile(db + "/calendar.one", everydate());
    writefile(db + "/calendar.two", everydate());

    std::string out = run(dir, "", "db");

    EXPECT_EQ(lines(out), 2 * span()) << out;
    EXPECT_TRUE(has(out, std::string(MARK) + " " + offsetday(0) + "\n")) << out;
    EXPECT_FALSE(has(out, "calendar.one:")) << out;
}

// A ./calendar shuts the fallback off rather than adding to it.
TEST(Calendar, LocalCalendarWinsOverDatabase)
{
    std::string dir = workdir("both");
    generator(dir);
    writefile(dir + "/calendar", everydate());

    std::string db = dir + "/db";
    EXPECT_EQ(std::system(("mkdir -p '" + db + "'").c_str()), 0);
    writefile(db + "/calendar.one", everydate());

    std::string out = run(dir, "", "db");

    EXPECT_EQ(lines(out), span()) << out;
}
