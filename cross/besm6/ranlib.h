#include "besm6/arch.h"

struct ranlib {
    word_t  ran_len;        /* 1 байт - длина имени в байтах */
    word_t  ran_off;        /* 4 байта - смещение в файле */
    char    *ran_name;      /* указатель на имя */
};

int fgetran(FILE *text, struct ranlib *sym);
void fputran(const struct ranlib *sym, FILE *file);
