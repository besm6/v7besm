//
// fsck: is this image self-consistent?
//
// Five passes, in the order the classic fsck runs them, because each depends on
// what the one before it learned:
//
//   1  Walk every allocated inode's block list.  Builds the block-in-use map and
//      catches addresses that are out of range or claimed twice.
//   2  Walk the directory tree from the root.  Builds the reference count for
//      every inode and catches entries pointing at unallocated ones.
//   3  Compare the reference counts against di_nlink.
//   4  Walk the free list.  Catches blocks that are both free and in use, blocks
//      free twice, and a chain that runs off the volume.
//   5  Account for every block: in use, free, or lost.
//
// The state is a class rather than the original's eight file-scope arrays, so a
// second run in the same process sees a clean slate -- and so cppcheck stops
// complaining, which under -Werror is the difference between building and not.
//
#ifndef B6FSUTIL_CHECK_H
#define B6FSUTIL_CHECK_H

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "filesystem.h"

class Checker {
public:
    Checker(Filesystem &f, const Options &o) : fs(f), opt(o) {}

    //
    // Run every pass.  Returns the number of problems found; 0 means the image is
    // consistent.  Nothing is repaired -- `-f' is not implemented, and an image
    // this tool builds is cheaper to rebuild than to repair.
    //
    int run(std::ostream &out);

    Checker(const Checker &)            = delete;
    Checker &operator=(const Checker &) = delete;

private:
    Filesystem &fs;
    const Options &opt;

    int nerror = 0;

    // How many times each block is claimed by a file, and by the free list.
    std::vector<uint8_t> used;
    std::vector<uint8_t> freed;

    // How many directory entries point at each inode, and what di_nlink says.
    std::vector<int32_t> refs;
    std::vector<int32_t> links;
    std::vector<uint8_t> allocated;
    std::vector<uint8_t> is_dir;
    std::vector<uint8_t> seen; // reached by the tree walk

    // The first path each inode was reached by, for readable messages.
    std::map<int64_t, std::string> names;

    std::ostream *os = nullptr;

    void error(const std::string &msg);

    void pass1_inodes();
    void pass2_directories();
    void pass3_link_counts();
    void pass4_free_list();
    void pass5_accounting();

    // Claim one block for inode `ino'; returns false if it was already claimed.
    bool claim(int64_t bno, int64_t ino, const char *what);
    void walk_indirect(int64_t bno, int level, int64_t ino);
    void walk_directory(int64_t ino, const std::string &path, int64_t parent);
};

#endif // B6FSUTIL_CHECK_H
