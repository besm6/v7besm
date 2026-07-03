#include "besm6/types.h"

struct ranlib {
    word_t ran_len; // 1 byte - name length in bytes
    word_t ran_off; // 4 bytes - offset in file
    char *ran_name; // pointer to the name
};

int fgetran(FILE *text, struct ranlib *sym);
void fputran(const struct ranlib *sym, FILE *file);
