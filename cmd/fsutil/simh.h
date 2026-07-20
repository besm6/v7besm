//
// The SIMH disk container, and conversion to and from the flat one.
//
// A flat image (image.h) is what the rest of this tool manipulates: block n at
// byte n * BSIZE, six-byte big-endian words, nothing else in the file.  It is easy
// to dump, easy to test, and SIMH cannot read it.  What SIMH attaches to an MD
// unit is a different animal entirely, and this file is the whole of the
// difference.  Everything here is transcribed from
// simh-besm6/BESM6/besm6_disk.c -- disk_attach()'s formatting loop at :380-400
// and the half-zone offsets in disk_write_track()/disk_read_track() at :490-620.
//
// THE THREE THINGS THAT DIFFER.
//
//   1. A word is EIGHT bytes, little-endian -- SIMH's t_value.  Not six, and not
//      big-endian.  (sim_fwrite is endian-independent and always produces a
//      little-endian file, whatever the host.)
//
//   2. Each word carries a two-bit TAG above its 48 bits of data:
//      SET_PARITY(x, PARITY_NUMBER) == (x & BITS48) | (2 << 48).  So a zero data
//      word on disk is 0x0002000000000000, not zero.  An image full of honest
//      zeroes is not a formatted disk.
//
//   3. Data is INTERLEAVED with per-zone service words.  A zone is 8 service
//      words followed by 1024 data words -- 1032 in all -- and a filesystem block
//      is a half-zone, or "track": block b lives in zone b/2, track b%2.  The
//      service words hold the zone's own address and a magic mark, and the drive
//      is 1000 zones, so a SIMH image is 1000*1032*8 = 8,256,000 bytes against the
//      flat image's 2000*512*6 = 6,144,000.
//
// The pleasing invariant that falls out of (3): the formatter writes
// control[0] = SET_PARITY(val << 36, 2) with val = 2*zone + track, which is
// exactly the filesystem block number.  from_simh() checks it at every half-zone,
// which is what catches a truncated or misaligned image.
//
#ifndef B6FSUTIL_SIMH_H
#define B6FSUTIL_SIMH_H

#include <string>

#include "fsutil.h"

namespace simh {

//
// besm6_defs.h:53 and the EC-5052 geometry in besm6_disk.c.
//
constexpr int64_t SYSWORDS  = 8;               // service words per zone
constexpr int64_t ZONE_SIZE = SYSWORDS + 1024; // words in a zone, 1032
constexpr int64_t NZONE     = 1000;            // zones `attach -n' formats
constexpr int64_t TPZ       = 2;               // tracks (half-zones) per zone
constexpr int64_t SIMH_WORD = 8;               // bytes per stored word
constexpr int64_t SIMH_SIZE = NZONE * ZONE_SIZE * SIMH_WORD;

//
// SET_PARITY(x, PARITY_NUMBER), besm6_defs.h:108-110.
//
constexpr Word TAG_NUMBER = Word(2) << 48;

//
// The mark disk_attach() stamps into every zone's second service word, and the
// field the volume number sits in beside it.
//
constexpr Word MAGIC_MARK  = Word(01370707) << 24;
constexpr int VOLUME_SHIFT = 12;
constexpr int VOLUME_MIN   = 2048;
constexpr int VOLUME_MAX   = 4095;

static_assert(NZONE * TPZ == MDNBLK, "the drive's zone count must match MDNBLK");

//
// SIMH takes the volume number from the RIGHTMOST run of digits in the filename
// stem -- "/var/tmp/besm6/2052.bin" is volume 2052 -- and rejects anything outside
// 2048..4095 (besm6_disk.c:352-365).  Reproduced exactly, so that a name this tool
// accepts is a name `attach' accepts.  Returns 0 if the stem holds no digits.
//
int volume_from_filename(const std::string &path);

//
// Flat -> SIMH.  `volume' must be in range; pass 0 to infer it from `out'.
// Refuses a flat image of more than MDNBLK blocks: the drive cannot address past
// that, and the failure would otherwise surface much later as an I/O error on a
// zone the backing file does not reach.  A shorter image is padded with formatted
// empty zones, exactly as `attach -n' would leave them.
//
void to_simh(const std::string &flat, const std::string &out, int volume);

//
// SIMH -> flat.  Validates the magic mark and every half-zone's self-address, and
// reports the volume number found.  `nblk' bounds how much is extracted; pass 0
// for the whole drive.
//
void from_simh(const std::string &in, const std::string &flat, int64_t nblk, int *volume);

//
// Write a freshly formatted, empty SIMH image -- byte for byte what
// `attach -n md00 <name><volume>.disk' produces.  Used by the round-trip path for
// padding, and by test/simh_test.cpp to compare against the real simulator.
//
void format(const std::string &out, int volume);

} // namespace simh

#endif // B6FSUTIL_SIMH_H
