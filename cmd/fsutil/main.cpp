//
// b6fsutil -- build and inspect Unix v7 filesystem images for the BESM-6.
//
// The image this tool writes is FLAT: a file of blocks, each 512 words, each word
// six big-endian bytes.  That is not what SIMH attaches -- see simh.h -- so an
// image destined for the simulator goes through `-S' on the way:
//
//     b6fsutil -n -M manifest.txt root.img
//     b6fsutil -c root.img
//     b6fsutil -S root.img md2053.disk
//     besm6> attach md00 md2053.disk
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include "check.h"
#include "command.h"
#include "create.h"
#include "manifest.h"
#include "simh.h"

namespace {

void usage()
{
    std::cerr << "BESM-6 Unix v7 filesystem utility\n"
              << "usage: b6fsutil [-v] image                     -- show the superblock\n"
              << "       b6fsutil -n [-s N] [-i N] [-M f] image  -- create a filesystem\n"
              << "       b6fsutil -c image                       -- check for consistency\n"
              << "       b6fsutil -a image path host-file        -- add one file\n"
              << "       b6fsutil -x image directory             -- extract everything\n"
              << "       b6fsutil -S [--volume=N] in out         -- convert to/from SIMH\n"
              << "\n"
              << "  -v          verbose; twice also lists the volume's contents\n"
              << "  -s N        volume size in blocks (default " << MDNBLK << ", one EC-5052)\n"
              << "  -i N        inodes to make room for (default one per two blocks)\n"
              << "  -M file     populate the new filesystem from a manifest\n"
              << "  -T N        timestamp to stamp the image with, for reproducible output\n"
              << "  --volume=N  SIMH volume number (2048..4095); taken from the output\n"
              << "              filename when omitted, exactly as `attach' does\n";
}

//
// Is this a SIMH disk image?  The magic mark in zone 0's second service word --
// the same one from_simh() validates -- so the direction of a conversion can be
// inferred instead of demanded.
//
bool looks_like_simh(const char *path)
{
    std::FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;

    Word w      = 0;
    bool got_it = true;
    std::fseek(f, 8, SEEK_SET); // word 1
    for (int i = 0; i < 8; i++) {
        const int c = std::fgetc(f);
        if (c == EOF) {
            got_it = false;
            break;
        }
        w |= Word(uint8_t(c)) << (8 * i);
    }
    std::fclose(f);

    return got_it && (w & simh::MAGIC_MARK) == simh::MAGIC_MARK;
}

int convert(const char *in, const char *out, int volume)
{
    if (looks_like_simh(in)) {
        int found = 0;
        simh::from_simh(in, out, 0, &found);
        std::cout << in << ": volume " << found << " -> " << out << " (flat)\n";
    } else {
        simh::to_simh(in, out, volume);
        std::cout << in << " -> " << out << " (SIMH volume "
                  << (volume ? volume : simh::volume_from_filename(out)) << ")\n";
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;

    bool make_new = false, do_simh = false, do_extract = false, do_add = false;
    bool do_check = false;
    int64_t nblk = MDNBLK, ninodes = 0, now = 0;
    int volume = 0;
    std::string manifest_file;

    int argi = 1;
    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];

        if (std::strcmp(a, "-v") == 0)
            opt.verbose++;
        else if (std::strcmp(a, "-n") == 0)
            make_new = true;
        else if (std::strcmp(a, "-S") == 0)
            do_simh = true;
        else if (std::strcmp(a, "-c") == 0)
            do_check = true;
        else if (std::strcmp(a, "-x") == 0)
            do_extract = true;
        else if (std::strcmp(a, "-a") == 0)
            do_add = true;
        else if (std::strcmp(a, "-s") == 0 && argi + 1 < argc)
            nblk = std::atoll(argv[++argi]);
        else if (std::strcmp(a, "-i") == 0 && argi + 1 < argc)
            ninodes = std::atoll(argv[++argi]);
        else if (std::strcmp(a, "-T") == 0 && argi + 1 < argc)
            now = std::atoll(argv[++argi]);
        else if (std::strcmp(a, "-M") == 0 && argi + 1 < argc)
            manifest_file = argv[++argi];
        else if (std::strncmp(a, "--volume=", 9) == 0)
            volume = std::atoi(a + 9);
        else {
            usage();
            return 1;
        }
    }

    const int nargs = argc - argi;
    if (nargs < 1) {
        usage();
        return 1;
    }

    try {
        if (do_simh) {
            if (nargs != 2) {
                usage();
                return 1;
            }
            return convert(argv[argi], argv[argi + 1], volume);
        }

        if (now == 0)
            now = int64_t(std::time(nullptr));

        const char *image = argv[argi];

        if (make_new) {
            Filesystem fs;
            create_filesystem(fs, image, nblk, ninodes, now);

            if (!manifest_file.empty()) {
                Manifest m;
                m.load(manifest_file);
                cmd::apply(fs, m, opt, now);
            }

            std::cout << image << ": " << fs.sb.fsize << " blocks, " << fs.inode_count()
                      << " inodes, " << fs.sb.tfree << " blocks free\n";
            fs.close();
            return 0;
        }

        if (do_check) {
            Filesystem fs;
            fs.open(image, false);

            Checker chk(fs, opt);
            const int problems = chk.run(std::cout);

            if (problems == 0)
                std::cout << image << ": clean\n";
            else
                std::cout << image << ": " << problems << " problem(s) found\n";

            fs.close();
            return problems == 0 ? 0 : 1;
        }

        if (do_add) {
            if (nargs != 3) {
                usage();
                return 1;
            }
            Filesystem fs;
            fs.open(image, true);
            cmd::add_file(fs, argv[argi + 1], argv[argi + 2], 0644, opt.uid, opt.gid, now);
            fs.sync(true);
            fs.close();
            return 0;
        }

        if (do_extract) {
            if (nargs != 2) {
                usage();
                return 1;
            }
            Filesystem fs;
            fs.open(image, false);
            cmd::extract(fs, argv[argi + 1], opt);
            fs.close();
            return 0;
        }

        //
        // No verb: show the superblock, and with -v the tree as well.
        //
        Filesystem fs;
        fs.open(image, false);

        if (!fs.sb.validate(std::cerr)) {
            std::cerr << image << ": not a BESM-6 filesystem\n";
            return 1;
        }
        fs.sb.print(std::cout);

        if (opt.verbose) {
            std::cout << "\n";
            cmd::list(fs, std::cout, opt);
        }
        fs.close();

    } catch (const FsError &e) {
        std::cerr << "b6fsutil: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
