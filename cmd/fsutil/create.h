//
// Making a filesystem.  See create.cpp for the ordering, which is the substance.
//
#ifndef B6FSUTIL_CREATE_H
#define B6FSUTIL_CREATE_H

#include <string>

#include "filesystem.h"

//
// Build a fresh filesystem in `path' and leave `fs' open on it.
//
//   nblk     size of the volume in blocks; MDNBLK (2000) is one EC-5052 drive
//   ninodes  how many inodes to make room for; 0 picks the default, one per two
//            blocks, and the figure is rounded up to fill the last i-list block
//   now      the timestamp for s_time and the root directory; 0 means time(NULL),
//            and a fixed value makes the output reproducible for the tests
//
void create_filesystem(Filesystem &fs, const std::string &path, int64_t nblk, int64_t ninodes = 0,
                       int64_t now = 0);

#endif // B6FSUTIL_CREATE_H
