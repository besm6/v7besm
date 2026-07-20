//
// The verbs: resolving a path, adding objects to an image, listing it, and
// extracting it back out.
//
// This is the layer that turns a manifest into a filesystem.  Everything below it
// deals in i-numbers and block numbers; everything here deals in paths.
//
#ifndef B6FSUTIL_COMMAND_H
#define B6FSUTIL_COMMAND_H

#include <ostream>
#include <string>

#include "inode.h"
#include "manifest.h"

namespace cmd {

//
// Resolve a path to an i-number, 0 if it does not exist.  Leading and repeated
// slashes are ignored; the walk starts at the root, since there is no notion of a
// current directory in an image.
//
int64_t namei(Filesystem &fs, const std::string &path);

//
// Resolve the DIRECTORY containing `path', creating intermediate directories if
// `create_missing'.  Returns its i-number and puts the final component in `leaf'.
//
int64_t parent_of(Filesystem &fs, const std::string &path, std::string &leaf, bool create_missing);

//
// Create one object.  Each returns the i-number it made.
//
int64_t make_directory(Filesystem &fs, const std::string &path, int64_t mode, int64_t uid,
                       int64_t gid, int64_t now);
int64_t add_file(Filesystem &fs, const std::string &path, const std::string &host_path,
                 int64_t mode, int64_t uid, int64_t gid, int64_t now);
int64_t add_device(Filesystem &fs, const std::string &path, bool block_device, int major, int minor,
                   int64_t mode, int64_t uid, int64_t gid, int64_t now);
void add_hard_link(Filesystem &fs, const std::string &path, const std::string &target);

//
// Apply a whole manifest, in order.  Directories are created before the things
// inside them, so a manifest that lists a file before its directory still works.
//
void apply(Filesystem &fs, const Manifest &m, const Options &opt, int64_t now);

//
// Walk the tree, one line per object, in the style of `ls -lR'.
//
void list(Filesystem &fs, std::ostream &out, const Options &opt);

//
// Write the whole tree out under `dest' on the host.  Devices become empty files
// -- the host will not let us make real ones, and an image being unpacked for
// inspection does not need them.
//
void extract(Filesystem &fs, const std::string &dest, const Options &opt);

} // namespace cmd

#endif // B6FSUTIL_COMMAND_H
