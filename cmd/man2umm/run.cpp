//
// b6man2umm's driver.  run() is the whole program and it RETURNS rather than
// calling exit(), the way cmd/nm's engine does, so the GoogleTest binary can drive
// it repeatedly in one process.  main.cpp beside it is three lines.
//
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "man2umm.h"

namespace umm {
namespace {

enum class Mode { Convert, Lint, Fonts, Dump };

void usage(std::ostream &os)
{
    os << "usage: b6man2umm [-w] [-o out.md] page.1     convert a roff -man page\n"
          "       b6man2umm [-w] -l page.1.umm ...       check canonical shape\n"
          "       b6man2umm -f page.1.umm                the font stream, for mancheck.sh\n"
          "       b6man2umm -d page                     dump the document model\n";
}

// A dialect page by its name, so that -l and -f read what -o wrote.
bool is_umm(const std::string &path)
{
    return path.size() > 4 && path.compare(path.size() - 4, 4, ".umm") == 0;
}

Doc read_any(const std::string &path, Diag &diag, bool &ok)
{
    ok = true;
    if (path == "-")
        return read_man(std::cin, "<stdin>", diag);
    std::ifstream in(path);
    if (!in) {
        std::cerr << "b6man2umm: " << path << ": cannot open\n";
        ok = false;
        return Doc();
    }
    return is_umm(path) ? read_umm(in, path, diag) : read_man(in, path, diag);
}

} // namespace

int cli(int argc, char **argv)
{
    Mode mode = Mode::Convert;
    std::string outpath;
    bool wflag = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-l")
            mode = Mode::Lint;
        else if (a == "-f")
            mode = Mode::Fonts;
        else if (a == "-d")
            mode = Mode::Dump;
        else if (a == "-w")
            wflag = true;
        else if (a == "-o") {
            if (++i >= argc) {
                usage(std::cerr);
                return 2;
            }
            outpath = argv[i];
        } else if (a.size() > 1 && a[0] == '-' && a != "-") {
            std::cerr << "b6man2umm: unknown flag " << a << '\n';
            usage(std::cerr);
            return 2;
        } else
            files.push_back(a);
    }

    if (files.empty()) {
        usage(std::cerr);
        return 2;
    }
    if (mode != Mode::Lint && files.size() > 1) {
        std::cerr << "b6man2umm: one file at a time unless -l\n";
        return 2;
    }

    Diag diag;
    int status = 0;

    for (const std::string &path : files) {
        bool ok = false;
        Doc doc = read_any(path, diag, ok);
        if (!ok) {
            status = 1;
            continue;
        }
        switch (mode) {
        case Mode::Convert: {
            lint(doc, outpath.empty() ? path : outpath, diag);
            if (outpath.empty()) {
                write_umm(std::cout, doc);
            } else {
                std::ofstream out(outpath);
                if (!out) {
                    std::cerr << "b6man2umm: " << outpath << ": cannot create\n";
                    return 1;
                }
                write_umm(out, doc);
            }
            break;
        }
        case Mode::Lint:
            lint(doc, path, diag);
            break;
        case Mode::Fonts:
            font_stream(std::cout, doc);
            break;
        case Mode::Dump:
            dump(std::cout, doc);
            break;
        }
    }

    diag.print(std::cerr);
    if (diag.errors() > 0 || (wflag && diag.warnings() > 0))
        status = 1;
    return status;
}

} // namespace umm
