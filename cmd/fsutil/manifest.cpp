//
// The manifest parser and the host-directory scanner.  See manifest.h for the
// format and for what changed from the RetroBSD original.
//
#include "manifest.h"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

//
// A keyword that starts a new object, as opposed to one that modifies the current
// one.  Returns 0 if it is not an object keyword.
//
char object_type(const std::string &word)
{
    if (word == "dir")
        return 'd';
    if (word == "file")
        return 'f';
    if (word == "link")
        return 'l';
    if (word == "bdev")
        return 'b';
    if (word == "cdev")
        return 'c';
    return 0;
}

//
// Numbers in a manifest are octal when they start with 0 -- they are mostly file
// modes -- and decimal otherwise, which is what strtol(.., 0) does apart from
// also accepting 0x.  Keeping strtol's behaviour means an existing manifest reads
// the same.
//
int64_t parse_number(const std::string &s, const std::string &where)
{
    try {
        size_t used     = 0;
        const int64_t v = std::stoll(s, &used, 0);
        if (used != s.size())
            throw std::invalid_argument("trailing");
        return v;
    } catch (const std::exception &) {
        throw FsError(where + ": expected a number, got `" + s + "'");
    }
}

std::string trim(const std::string &s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

void Manifest::load(const std::string &filename)
{
    std::ifstream in(filename);
    if (!in)
        throw FsError(filename + ": cannot open");

    list.clear();

    //
    // The original ends its loop with `fd = 0; goto newobj;' -- a jump back INTO
    // the loop body to flush the last section -- and frees a pointer the caller
    // may still hold.  Here the current object is simply held in `cur' and pushed
    // when the next object keyword arrives or the file ends.
    //
    ManifestEntry cur;
    bool have_cur = false;

    auto flush = [&]() {
        if (!have_cur)
            return;
        if (cur.mode < 0)
            cur.mode = (cur.type == 'd') ? dirmode : filemode;
        if (cur.owner < 0)
            cur.owner = owner;
        if (cur.group < 0)
            cur.group = group;
        list.push_back(cur);
        have_cur = false;
    };

    std::string line;
    int lineno = 0;

    while (std::getline(in, line)) {
        lineno++;

        // Strip a comment, then whitespace.
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        line = trim(line);
        if (line.empty())
            continue;

        std::istringstream ls(line);
        std::string word, arg;
        ls >> word;
        std::getline(ls, arg);
        arg = trim(arg);

        const std::string where = filename + ":" + std::to_string(lineno);

        if (word == "default") {
            flush();
            continue;
        }

        if (const char t = object_type(word)) {
            flush();
            cur      = ManifestEntry{};
            cur.type = t;
            cur.path = arg;
            have_cur = true;
            if (cur.path.empty())
                throw FsError(where + ": `" + word + "' needs a path");
            continue;
        }

        //
        // `symlink' is refused rather than ignored.  Silently dropping it would
        // produce an image that is missing files the manifest asked for.
        //
        if (word == "symlink")
            throw FsError(where +
                          ": this filesystem has no symbolic links "
                          "(include/sys/stat.h has no S_IFLNK)");

        // Everything else modifies the current object, or the defaults.
        if (word == "owner" || word == "group" || word == "mode" || word == "dirmode" ||
            word == "filemode" || word == "major" || word == "minor" || word == "target" ||
            word == "source") {
            if (word == "target" || word == "source") {
                if (!have_cur)
                    throw FsError(where + ": `" + word + "' outside an object");
                (word == "target" ? cur.target : cur.source) = arg;
                continue;
            }

            const int64_t v = parse_number(arg, where);

            if (!have_cur) {
                // Part of a `default' block.
                if (word == "owner")
                    owner = v;
                else if (word == "group")
                    group = v;
                else if (word == "dirmode")
                    dirmode = v;
                else if (word == "filemode")
                    filemode = v;
                else
                    throw FsError(where + ": `" + word + "' outside an object");
                continue;
            }

            if (word == "owner")
                cur.owner = v;
            else if (word == "group")
                cur.group = v;
            else if (word == "mode")
                cur.mode = v;
            else if (word == "major")
                cur.major = int(v);
            else if (word == "minor")
                cur.minor = int(v);
            else
                throw FsError(where + ": `" + word + "' does not apply here");
            continue;
        }

        throw FsError(where + ": unknown keyword `" + word + "'");
    }

    flush();
}

void Manifest::scan(const std::string &dirname)
{
    list.clear();

    if (!fs::is_directory(dirname))
        throw FsError(dirname + ": not a directory");

    //
    // Hard links: std::filesystem reports a link count but not (dev, ino), so the
    // pair comes from lstat.  The first path seen for an inode becomes the file;
    // every later one becomes a `link' pointing at it.
    //
    std::map<std::pair<uint64_t, uint64_t>, std::string> links;

    //
    // Collected and sorted rather than emitted as encountered.  fts(3) sorted its
    // output and the recursive_directory_iterator does not, and a manifest is a
    // build input -- two runs over the same tree must produce the same file.
    //
    std::vector<fs::path> paths;
    for (auto it = fs::recursive_directory_iterator(dirname,
                                                    fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());

    for (const fs::path &p : paths) {
        // The path as it will appear in the image.
        std::string rel = fs::relative(p, dirname).generic_string();
        if (rel.empty() || rel == ".")
            continue;
        const std::string image_path = "/" + rel;

        struct stat st;
        if (::lstat(p.c_str(), &st) != 0)
            throw FsError(p.string() + ": cannot stat");

        ManifestEntry e;
        e.path  = image_path;
        e.mode  = st.st_mode & 07777;
        e.owner = st.st_uid;
        e.group = st.st_gid;

        if (S_ISLNK(st.st_mode))
            throw FsError(p.string() +
                          ": symbolic link, which this filesystem "
                          "cannot represent");

        if (S_ISDIR(st.st_mode)) {
            e.type = 'd';
        } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            e.type  = S_ISCHR(st.st_mode) ? 'c' : 'b';
            e.major = int((st.st_rdev >> 8) & 0377);
            e.minor = int(st.st_rdev & 0377);
        } else if (S_ISREG(st.st_mode)) {
            const auto key = std::make_pair(uint64_t(st.st_dev), uint64_t(st.st_ino));
            if (st.st_nlink > 1) {
                auto found = links.find(key);
                if (found != links.end()) {
                    e.type   = 'l';
                    e.target = found->second;
                    list.push_back(e);
                    continue;
                }
                links[key] = image_path;
            }
            e.type   = 'f';
            e.source = p.string();
        } else {
            // Sockets, fifos and the like: not represented, and not silently skipped.
            throw FsError(p.string() + ": unsupported file type");
        }

        list.push_back(e);
    }
}

void Manifest::print(std::ostream &out) const
{
    out << "#\n# Generated by b6fsutil.\n#\n";
    out << "default\n";
    out << "owner " << owner << "\n";
    out << "group " << group << "\n";
    out << "dirmode 0" << std::oct << dirmode << std::dec << "\n";
    out << "filemode 0" << std::oct << filemode << std::dec << "\n";

    for (const ManifestEntry &e : list) {
        out << "\n";
        switch (e.type) {
        case 'd':
            out << "dir " << e.path << "\n";
            break;
        case 'f':
            out << "file " << e.path << "\n";
            if (!e.source.empty())
                out << "source " << e.source << "\n";
            break;
        case 'l':
            out << "link " << e.path << "\ntarget " << e.target << "\n";
            break;
        case 'b':
        case 'c':
            out << (e.type == 'b' ? "bdev " : "cdev ") << e.path << "\n";
            out << "major " << e.major << "\nminor " << e.minor << "\n";
            break;
        default:
            break;
        }
        if (e.mode >= 0)
            out << "mode 0" << std::oct << e.mode << std::dec << "\n";
        if (e.owner != owner)
            out << "owner " << e.owner << "\n";
        if (e.group != group)
            out << "group " << e.group << "\n";
    }
}
