//
// The verbs.  See command.h.
//
#include "command.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "dir.h"

namespace fs_std = std::filesystem;

namespace cmd {
namespace {

//
// Split a path into components, dropping empty ones so that "//a///b" and "/a/b"
// are the same path.
//
std::vector<std::string> split(const std::string &path)
{
    std::vector<std::string> out;
    size_t i = 0;

    while (i < path.size()) {
        while (i < path.size() && path[i] == '/')
            i++;
        const size_t start = i;
        while (i < path.size() && path[i] != '/')
            i++;
        if (i > start)
            out.push_back(path.substr(start, i - start));
    }
    return out;
}

//
// Allocate an inode and initialise it.  The mode is set and written back
// immediately: ialloc() decides an inode is free by finding di_mode == 0, so an
// allocated-but-unwritten inode would be handed out again by the next scan.
//
int64_t new_inode(Filesystem &fs, Inode &ip, int64_t mode, int64_t uid, int64_t gid, int64_t now)
{
    const int64_t ino = fs.inode_alloc();

    ip.get(fs, ino);
    ip.clear();
    ip.mode  = mode;
    ip.nlink = 1;
    ip.uid   = uid;
    ip.gid   = gid;
    ip.atime = now;
    ip.mtime = now;
    ip.ctime = now;
    ip.save(true);

    return ino;
}

void warn_if_truncated(const std::string &leaf, const std::string &path, const Options &opt)
{
    if (dir::name_is_truncated(leaf) && opt.verbose >= 0) {
        std::cerr << "b6fsutil: warning: " << path << ": name truncated to " << DIRSIZ
                  << " characters (\"" << leaf.substr(0, DIRSIZ) << "\")\n";
    }
}

} // namespace

int64_t namei(Filesystem &fs, const std::string &path)
{
    int64_t ino = ROOTINO;

    for (const std::string &part : split(path)) {
        Inode dp;
        dp.get(fs, ino);
        if (!dp.is_dir())
            return 0;

        ino = dir::lookup(dp, part);
        if (ino == 0)
            return 0;
    }
    return ino;
}

int64_t parent_of(Filesystem &fs, const std::string &path, std::string &leaf, bool create_missing)
{
    std::vector<std::string> parts = split(path);
    if (parts.empty())
        throw FsError("`" + path + "' names the root, which already exists");

    leaf = parts.back();
    parts.pop_back();

    int64_t ino = ROOTINO;
    std::string sofar;

    for (const std::string &part : parts) {
        sofar += "/" + part;

        Inode dp;
        dp.get(fs, ino);
        if (!dp.is_dir())
            throw FsError(sofar + ": not a directory");

        int64_t next = dir::lookup(dp, part);
        if (next == 0) {
            if (!create_missing)
                throw FsError(sofar + ": no such directory");

            //
            // Create the missing intermediate directory.  Its mode is the
            // conventional 0755 rather than the manifest's dirmode: a directory
            // nobody asked for should not inherit a permission somebody chose for
            // one they did.
            //
            Inode nd;
            next = new_inode(fs, nd, IFDIR | 0755, 0, 0, dp.mtime);
            dir::make_empty(nd, ino);
            nd.nlink = 2;
            nd.save(true);

            dir::enter(dp, part, next);
            dp.nlink++;
            dp.save(true);
        }
        ino = next;
    }
    return ino;
}

int64_t make_directory(Filesystem &fs, const std::string &path, int64_t mode, int64_t uid,
                       int64_t gid, int64_t now)
{
    std::string leaf;
    const int64_t parent = parent_of(fs, path, leaf, true);

    Inode pd;
    pd.get(fs, parent);

    if (const int64_t existing = dir::lookup(pd, leaf)) {
        Inode ep;
        ep.get(fs, existing);
        if (!ep.is_dir())
            throw FsError(path + ": exists and is not a directory");
        return existing; // already there, e.g. created as an intermediate
    }

    Inode nd;
    const int64_t ino = new_inode(fs, nd, IFDIR | (mode & 07777), uid, gid, now);
    dir::make_empty(nd, parent);
    nd.nlink = 2; // `.' and the entry about to go in the parent
    nd.save(true);

    dir::enter(pd, leaf, ino);
    pd.nlink++; // the new directory's `..'
    pd.save(true);

    return ino;
}

int64_t add_file(Filesystem &fs, const std::string &path, const std::string &host_path,
                 int64_t mode, int64_t uid, int64_t gid, int64_t now)
{
    std::string leaf;
    const int64_t parent = parent_of(fs, path, leaf, true);

    Inode pd;
    pd.get(fs, parent);
    if (dir::lookup(pd, leaf) != 0)
        throw FsError(path + ": already exists");

    Inode ip;
    const int64_t ino = new_inode(fs, ip, IFREG | (mode & 07777), uid, gid, now);

    if (!host_path.empty()) {
        std::ifstream in(host_path, std::ios::binary);
        if (!in)
            throw FsError(host_path + ": cannot open");

        //
        // Copied a block at a time rather than slurped: an image is 6 Mb but the
        // host file it is fed could be anything, and there is no reason to hold
        // all of it.
        //
        std::vector<uint8_t> buf(BSIZE);
        int64_t off = 0;
        for (;;) {
            in.read(reinterpret_cast<char *>(buf.data()), BSIZE);
            const int64_t got = in.gcount();
            if (got <= 0)
                break;
            ip.write(off, buf.data(), got);
            off += got;
        }
    }

    ip.save(true);
    dir::enter(pd, leaf, ino);
    pd.save(true);

    return ino;
}

int64_t add_device(Filesystem &fs, const std::string &path, bool block_device, int major, int minor,
                   int64_t mode, int64_t uid, int64_t gid, int64_t now)
{
    if (major < 0 || major > 255 || minor < 0 || minor > 255)
        throw FsError(path + ": major and minor must be 0..255");

    std::string leaf;
    const int64_t parent = parent_of(fs, path, leaf, true);

    Inode pd;
    pd.get(fs, parent);
    if (dir::lookup(pd, leaf) != 0)
        throw FsError(path + ": already exists");

    Inode ip;
    const int64_t ino =
        new_inode(fs, ip, (block_device ? IFBLK : IFCHR) | (mode & 07777), uid, gid, now);

    //
    // di_addr[0] doubles as di_rdev -- include/sys/ino.h says so.  The RetroBSD
    // source puts it in addr[1], which is a different field on a different
    // filesystem.
    //
    ip.set_rdev(makedev(major, minor));
    ip.save(true);

    dir::enter(pd, leaf, ino);
    pd.save(true);

    return ino;
}

void add_hard_link(Filesystem &fs, const std::string &path, const std::string &target)
{
    const int64_t ino = namei(fs, target);
    if (ino == 0)
        throw FsError(path + ": link target `" + target + "' does not exist");

    Inode ip;
    ip.get(fs, ino);
    if (ip.is_dir())
        throw FsError(path + ": cannot hard-link a directory");

    std::string leaf;
    const int64_t parent = parent_of(fs, path, leaf, true);

    Inode pd;
    pd.get(fs, parent);
    if (dir::lookup(pd, leaf) != 0)
        throw FsError(path + ": already exists");

    dir::enter(pd, leaf, ino);
    pd.save(true);

    ip.nlink++;
    ip.dirty = true;
    ip.save();
}

void apply(Filesystem &fs, const Manifest &m, const Options &opt, int64_t now)
{
    //
    // Directories first, then everything else.  A manifest is allowed to mention
    // /etc/passwd before /etc, and parent_of() would create /etc with a default
    // mode -- losing the mode the manifest asked for.  Two passes cost nothing and
    // remove the ordering requirement.
    //
    for (const ManifestEntry &e : m.entries()) {
        if (e.type != 'd')
            continue;
        make_directory(fs, e.path, e.mode, e.owner, e.group, now);
        if (opt.verbose)
            std::cout << "dir  " << e.path << "\n";
    }

    //
    // Hard links last, so their target exists whatever order the manifest is in.
    //
    for (const ManifestEntry &e : m.entries()) {
        std::string leaf;
        const std::vector<std::string> parts = split(e.path);
        if (!parts.empty())
            leaf = parts.back();

        switch (e.type) {
        case 'd':
        case 'l':
            continue;
        case 'f':
            warn_if_truncated(leaf, e.path, opt);
            add_file(fs, e.path, e.source.empty() ? e.path : e.source, e.mode, e.owner, e.group,
                     now);
            if (opt.verbose)
                std::cout << "file " << e.path << "\n";
            break;
        case 'b':
        case 'c':
            warn_if_truncated(leaf, e.path, opt);
            add_device(fs, e.path, e.type == 'b', e.major, e.minor, e.mode, e.owner, e.group, now);
            if (opt.verbose)
                std::cout << "dev  " << e.path << "\n";
            break;
        default:
            throw FsError(e.path + ": unknown manifest object type");
        }
    }

    for (const ManifestEntry &e : m.entries()) {
        if (e.type != 'l')
            continue;
        add_hard_link(fs, e.path, e.target);
        if (opt.verbose)
            std::cout << "link " << e.path << " -> " << e.target << "\n";
    }

    fs.sync(true);
}

namespace {

//
// Walk the tree depth-first, calling `fn' for every object with its full path.
// `.' and `..' are skipped, so the walk terminates without needing a seen-set.
//
void walk(Filesystem &fs, int64_t ino, const std::string &path,
          const std::function<void(const std::string &, int64_t, Inode &)> &fn)
{
    Inode ip;
    ip.get(fs, ino);
    fn(path, ino, ip);

    if (!ip.is_dir())
        return;

    std::vector<std::pair<std::string, int64_t>> kids;
    dir::each(ip, [&](int64_t, const DirEntry &e) {
        if (e.ino == 0)
            return;
        if (std::strcmp(e.name, ".") == 0 || std::strcmp(e.name, "..") == 0)
            return;
        kids.emplace_back(e.name, e.ino);
    });

    for (const auto &kid : kids)
        walk(fs, kid.second, path + "/" + kid.first, fn);
}

char type_letter(const Inode &ip)
{
    switch (ip.mode & IFMT) {
    case IFDIR:
        return 'd';
    case IFCHR:
        return 'c';
    case IFBLK:
        return 'b';
    case IFREG:
        return '-';
    default:
        return '?';
    }
}

} // namespace

void list(Filesystem &fs, std::ostream &out, const Options &)
{
    //
    // The inode is taken by non-const reference because walk() shares one callback
    // signature with extract(), which calls Inode::read() -- and read() maps blocks,
    // so it cannot be const.  Listing does not modify it.
    //
    // cppcheck-suppress constParameterReference
    walk(fs, ROOTINO, "", [&](const std::string &path, int64_t ino, Inode &ip) {
        out << type_letter(ip) << " " << std::oct << (ip.mode & 07777) << std::dec << " ";
        out << ip.uid << "/" << ip.gid << " ";

        if (ip.is_dev())
            out << "dev " << major_of(ip.rdev()) << "," << minor_of(ip.rdev()) << " ";
        else
            out << ip.size << " ";

        out << "ino " << ino << " nlink " << ip.nlink << "  " << (path.empty() ? "/" : path)
            << "\n";
    });
}

void extract(Filesystem &fs, const std::string &dest, const Options &opt)
{
    fs_std::create_directories(dest);

    walk(fs, ROOTINO, "", [&](const std::string &path, int64_t, Inode &ip) {
        const std::string out = dest + (path.empty() ? "/" : path);

        if (ip.is_dir()) {
            fs_std::create_directories(out);
            return;
        }

        if (ip.is_dev()) {
            //
            // A device node cannot be recreated on the host without privilege, and
            // an image being unpacked for inspection does not need one.  An empty
            // placeholder keeps the tree shape; -v says what was skipped.
            //
            std::ofstream(out).close();
            if (opt.verbose)
                std::cout << "device " << path << " extracted as an empty file\n";
            return;
        }

        std::ofstream f(out, std::ios::binary);
        if (!f)
            throw FsError(out + ": cannot create");

        std::vector<uint8_t> buf(BSIZE);
        for (int64_t off = 0; off < ip.size;) {
            const int64_t got = ip.read(off, buf.data(), BSIZE);
            if (got <= 0)
                break;
            f.write(reinterpret_cast<const char *>(buf.data()), got);
            off += got;
        }
    });
}

} // namespace cmd
