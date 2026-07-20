//
// Flat <-> SIMH container conversion.  See simh.h for the format itself.
//
#include "simh.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "image.h"

namespace simh {
namespace {

//
// One stored word: eight bytes, LITTLE-endian, tag in bits 49-50.
//
// Note the direction: this is the opposite of the flat container's six-byte
// big-endian word, and the two live in the same tool.  Neither is wrong -- the
// flat one matches b6as and b6ld, this one matches SIMH's t_value -- but they must
// not be confused, which is why the SIMH pair lives here and nowhere else.
//
void put_simh_word(std::FILE *f, Word w)
{
    for (int i = 0; i < SIMH_WORD; i++)
        std::fputc(int((w >> (8 * i)) & 0xFF), f);
}

Word get_simh_word(std::FILE *f)
{
    Word w = 0;
    for (int i = 0; i < SIMH_WORD; i++) {
        const int c = std::fgetc(f);
        if (c == EOF)
            throw FsError("unexpected end of SIMH image");
        w |= Word(uint8_t(c)) << (8 * i);
    }
    return w;
}

//
// The four service words of one half-zone, as disk_attach() writes them.
//
// control[0] carries the half-zone's own address in bits 37-48 -- and because the
// formatter computes it as 2*zone + track, that address IS the filesystem block
// number.  control[1] carries the magic mark and the volume; [2] and [3] are the
// userid and checksum the real controller would fill in, left as bare tags.
//
void put_sysdata(std::FILE *f, int64_t blk, int volume)
{
    put_simh_word(f, TAG_NUMBER | (Word(blk) << 36));
    put_simh_word(f, TAG_NUMBER | MAGIC_MARK | (Word(volume) << VOLUME_SHIFT));
    put_simh_word(f, TAG_NUMBER);
    put_simh_word(f, TAG_NUMBER);
}

std::FILE *open_or_throw(const std::string &path, const char *mode)
{
    std::FILE *f = std::fopen(path.c_str(), mode);
    if (!f)
        throw FsError(path + ": " + std::strerror(errno));
    return f;
}

int check_volume(int volume, const std::string &path)
{
    if (volume == 0)
        volume = volume_from_filename(path);

    if (volume < VOLUME_MIN || volume > VOLUME_MAX) {
        if (volume == 0)
            throw FsError(path + ": filename must contain volume number " +
                          std::to_string(VOLUME_MIN) + ".." + std::to_string(VOLUME_MAX));
        throw FsError("disk volume " + std::to_string(volume) + " invalid (must be " +
                      std::to_string(VOLUME_MIN) + ".." + std::to_string(VOLUME_MAX) + ")");
    }
    return volume;
}

} // namespace

//
// besm6_disk.c:352-361, transcribed: take the filename stem, scan back from its
// end for the last digit, back up over the run of digits, and read a number.
//
int volume_from_filename(const std::string &path)
{
    // The stem: basename with any extension removed.
    size_t begin = path.find_last_of("/\\");
    begin        = (begin == std::string::npos) ? 0 : begin + 1;
    size_t end   = path.find_last_of('.');
    if (end == std::string::npos || end < begin)
        end = path.size();

    const std::string stem = path.substr(begin, end - begin);
    if (stem.empty())
        return 0;

    size_t pos = stem.size();
    while (pos > 0 && !std::isdigit(uint8_t(stem[pos - 1])))
        pos--;
    if (pos == 0)
        return 0; // no digits at all

    size_t first = pos;
    while (first > 0 && std::isdigit(uint8_t(stem[first - 1])))
        first--;

    return std::atoi(stem.substr(first, pos - first).c_str());
}

void format(const std::string &out, int volume)
{
    volume = check_volume(volume, out);

    std::FILE *f = open_or_throw(out, "wb");
    for (int64_t zone = 0; zone < NZONE; zone++) {
        put_sysdata(f, zone * TPZ, volume);
        put_sysdata(f, zone * TPZ + 1, volume);
        for (int64_t w = 0; w < ZONE_SIZE - SYSWORDS; w++)
            put_simh_word(f, TAG_NUMBER);
    }
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad)
        throw FsError(out + ": write failed");
}

void to_simh(const std::string &flat, const std::string &out, int volume)
{
    volume = check_volume(volume, out);

    Image img;
    img.open(flat, false);

    if (img.nblocks() > MDNBLK)
        throw FsError(flat + ": " + std::to_string(img.nblocks()) +
                      " blocks does not fit an EC-5052 drive (" + std::to_string(MDNBLK) +
                      "); the controller cannot address past it");

    std::FILE *f = open_or_throw(out, "wb");

    Block b{};
    for (int64_t zone = 0; zone < NZONE; zone++) {
        //
        // Both half-zones' service words come first, then all 1024 data words --
        // the order disk_attach() writes and disk_write_track() seeks into.  It is
        // NOT four service words followed by that track's 512 data words.
        //
        put_sysdata(f, zone * TPZ, volume);
        put_sysdata(f, zone * TPZ + 1, volume);

        for (int64_t track = 0; track < TPZ; track++) {
            const int64_t blk = zone * TPZ + track;

            // Past the end of a short flat image: leave the zone formatted-empty.
            if (blk < img.nblocks())
                img.read_block(blk, b);
            else
                b.fill(0);

            for (int i = 0; i < BSIZEW; i++)
                put_simh_word(f, TAG_NUMBER | (b[i] & WORD_MASK));
        }
    }

    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad)
        throw FsError(out + ": write failed");
}

void from_simh(const std::string &in, const std::string &flat, int64_t nblk, int *volume)
{
    if (nblk <= 0)
        nblk = MDNBLK;
    if (nblk > MDNBLK)
        throw FsError("cannot extract more than " + std::to_string(MDNBLK) + " blocks");

    std::FILE *f = open_or_throw(in, "rb");

    //
    // The number of whole zones actually present.  A short file is tolerated --
    // the tail is zero-filled -- but a file with no whole zone at all is not an
    // image and says so.
    //
    std::fseek(f, 0, SEEK_END);
    const int64_t size   = std::ftell(f);
    const int64_t zbytes = ZONE_SIZE * SIMH_WORD;
    const int64_t have   = size / zbytes;
    if (have < 1) {
        std::fclose(f);
        throw FsError(in + ": too short to be a SIMH disk image");
    }

    Image img;
    img.create(flat, nblk);

    int found = 0;

    try {
        for (int64_t zone = 0; zone * TPZ < nblk; zone++) {
            if (zone >= have)
                break; // short image: the rest stays zero

            std::fseek(f, long(zone * zbytes), SEEK_SET);

            std::array<Word, SYSWORDS> sysdata{};
            std::generate(sysdata.begin(), sysdata.end(), [f] { return get_simh_word(f); });

            //
            // Validate the zone before trusting a word of its data.  The magic
            // mark says this is a formatted BESM-6 disk at all; the self-address
            // says we are aligned to a zone boundary and not reading someone
            // else's file at an offset that happens to be a multiple of 8256.
            //
            if ((sysdata[1] & MAGIC_MARK) != MAGIC_MARK)
                throw FsError(in + ": not a formatted BESM-6 disk image (zone " +
                              std::to_string(zone) + " has no magic mark)");

            for (int64_t track = 0; track < TPZ; track++) {
                const int64_t blk   = zone * TPZ + track;
                const Word recorded = (sysdata[track * 4] >> 36) & 07777;
                if (recorded != Word(blk))
                    throw FsError(in + ": zone address mismatch at block " + std::to_string(blk) +
                                  " (image says " + std::to_string(recorded) + ")");
            }

            if (zone == 0)
                found = int((sysdata[1] >> VOLUME_SHIFT) & 07777);

            for (int64_t track = 0; track < TPZ; track++) {
                const int64_t blk = zone * TPZ + track;
                if (blk >= nblk)
                    break;
                Block b{};
                for (int i = 0; i < BSIZEW; i++)
                    b[i] = get_simh_word(f) & WORD_MASK;
                img.write_block(blk, b);
            }
        }
    } catch (...) {
        std::fclose(f);
        throw;
    }

    std::fclose(f);
    if (volume)
        *volume = found;
}

} // namespace simh
