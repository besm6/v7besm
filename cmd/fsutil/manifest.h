//
// The manifest: a line-oriented description of a filesystem tree.
//
// This layer knows nothing about the BESM-6 or about v7 -- it is the one part of
// the RetroBSD original that transferred almost unchanged, because it describes
// what to build rather than how to lay it out.  What did change:
//
//   THERE IS NO `symlink'.  include/sys/stat.h has no S_IFLNK; this v7 predates
//   symbolic links entirely.  The keyword is rejected rather than given some
//   invented encoding, and `target' now belongs to `link' alone.
//
//   The linked list of malloc'd flexible-array records became a std::vector, and
//   fts(3) became std::filesystem -- which also disposes of the #ifdef'd
//   ftsent_compare whose signature differed between BSD and Linux.
//
// The file format, unchanged apart from the above:
//
//     # a comment
//     default            -- the defaults below apply from here
//     owner 0
//     group 0
//     dirmode 0775
//     filemode 0664
//
//     dir /tmp
//     file /etc/passwd   [mode 0444] [owner N] [group N]
//     link /etc/aliases  target mail/aliases
//     cdev /dev/tty      major 5  minor 0
//     bdev /dev/sda      major 8  minor 0
//
#ifndef B6FSUTIL_MANIFEST_H
#define B6FSUTIL_MANIFEST_H

#include <iosfwd>
#include <string>
#include <vector>

#include "fsutil.h"

//
// One object to create.  `type' follows the original's single letters so that
// existing manifests and existing habits still work:
//
//     d  directory        f  regular file       l  hard link
//     b  block device     c  character device
//
struct ManifestEntry {
    char type = 0;
    std::string path;   // where it goes in the image
    std::string source; // where to read it from on the host (files only)
    std::string target; // what a hard link points at, relative to the image root
    int64_t mode  = -1;
    int64_t owner = -1;
    int64_t group = -1;
    int major     = -1;
    int minor     = -1;
};

class Manifest {
public:
    //
    // Read a manifest file.  Throws FsError with a line number on a syntax error.
    //
    void load(const std::string &filename);

    //
    // Build a manifest by walking a host directory, so that a tree can be turned
    // into an image without writing one by hand.  Hard links are detected and
    // emitted as `link' entries; a symbolic link is an error, since the target
    // filesystem cannot represent one.
    //
    void scan(const std::string &dirname);

    void print(std::ostream &out) const;

    const std::vector<ManifestEntry> &entries() const { return list; }

    // Defaults, as set by the `default' block.
    int64_t filemode = 0664;
    int64_t dirmode  = 0775;
    int64_t owner    = 0;
    int64_t group    = 0;

private:
    std::vector<ManifestEntry> list;
};

#endif // B6FSUTIL_MANIFEST_H
