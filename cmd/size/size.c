/*
 *      ОС ДЕМОС СВС-Б.
 *
 *      size [-w] [file ...]    - выдать размеры сегментов объектного файла.
 *                                Если задан флаг "-w", размеры выдаются
 *                                в словах, иначе - в байтах.
 *
 *      Автор: Вакуленко С.В. (МФТИ).
 */

#include <stdio.h>
#include <stdlib.h>

#include "besm6/b.out.h"

#include "size.h"

#define W 6 /* длина слова в байтах */

static int header; /* был ли уже напечатан заголовок */
static int wflag;  /* выдавать длину в словах */

#define MSG(l, r) (msg ? (r) : (l))

static char msg;

static void initmsg(void)
{
    const char *p;

    msg = (p = getenv("MSG")) && *p == 'r';
}

static void size(const char *fname)
{
    struct exec buf;
    long sum;
    FILE *f;

    if ((f = fopen(fname, "r")) == NULL) {
        printf(MSG("size: %s not found\n", "size: %s не найден\n"), fname);
        return;
    }
    if (!fgethdr(f, &buf) || N_BADMAG(buf)) {
        printf(MSG("size: %s not an object file\n", "size: %s не объектный файл\n"), fname);
        fclose(f);
        return;
    }
    if (header == 0) {
        printf("const\ttext\tdata\tbss\tabss\tdec\thex\n");
        header = 1;
    }
    sum = buf.a_const + buf.a_text + buf.a_data + buf.a_bss + buf.a_abss;
    if (wflag) {
        sum /= W;
        printf("%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%lx\t%s\n", (long)buf.a_const / W,
               (long)buf.a_text / W, (long)buf.a_data / W, (long)buf.a_bss / W,
               (long)buf.a_abss / W, sum, sum, fname);
    } else {
        printf("%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%lx\t%s\n", (long)buf.a_const, (long)buf.a_text,
               (long)buf.a_data, (long)buf.a_bss, (long)buf.a_abss, sum, sum, fname);
    }
    fclose(f);
}

int size_run(int argc, char **argv)
{
    int yesarg = 0; /* были ли параметры - имена файлов */

    /* Reset option state so repeated in-process runs start clean. */
    header = wflag = 0;

    initmsg();
    while (--argc) {
        ++argv;
        if (**argv == '-') {
            while (*++*argv)
                switch (**argv) {
                case 'w':
                    wflag++;
                    break;
                default:
                    fprintf(stderr, MSG("size: bad flag %c\n", "size: неизвестный флаг %c\n"),
                            **argv);
                    return 1;
                }
            continue;
        }
        size(*argv);
        yesarg = 1;
    }
    if (!yesarg)
        size("a.out");
    return 0;
}
