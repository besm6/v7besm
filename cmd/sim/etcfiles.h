//
// The target's own /etc, compiled into b6sim.  etcfiles.cpp is the account of why and the
// six files themselves; this is the interface, and it is deliberately the shape of
// SimKernel::dev_minor(): an EXACT path match, answered from a table, or nothing.
//
#ifndef DUBNA_ETCFILES_H
#define DUBNA_ETCFILES_H

#include <string>

class EtcFiles {
public:
    struct File {
        const char *path; // the guest path, e.g. "/etc/passwd"
        const char *text; // the whole file, byte for byte
        unsigned size;    // bytes, NOT counting the literal's NUL
    };

    // nullptr for a path b6sim does not serve.
    static const File *find(const std::string &path);

    // The table itself, so cmd/sim/test/etc_test.cpp can walk it: that program is the whole
    // guard on the transcription below, and it cannot check a row it cannot see.
    static int count();
    static const File &entry(int i);
};

#endif // DUBNA_ETCFILES_H
