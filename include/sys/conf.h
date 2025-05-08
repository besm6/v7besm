/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

struct buf; /* forward declarations */
struct tty;

/*
 * Declaration of block device
 * switch. Each entry (row) is
 * the only link between the
 * main unix code and the driver.
 * The initialization of the
 * device switches is in the
 * file conf.c.
 */
extern struct bdevsw {
    void (*d_open)(dev_t, int);
    void (*d_close)(dev_t, int);
    int (*d_strategy)(struct buf *);
    struct buf *d_tab;
} bdevsw[];

/*
 * Character device switch.
 */
extern struct cdevsw {
    void (*d_open)(dev_t, int);
    void (*d_close)(dev_t, int);
    void (*d_read)(dev_t);
    void (*d_write)(dev_t);
    void (*d_ioctl)(dev_t, int, caddr_t, int);
    void (*d_stop)(struct tty *);
    struct tty *d_ttys;
} cdevsw[];

/*
 * tty line control switch.
 */
extern struct linesw {
    void (*l_open)(dev_t, struct tty *);
    void (*l_close)(struct tty *);
    int (*l_read)(struct tty *);
    caddr_t (*l_write)(struct tty *);
    void (*l_ioctl)(int, struct tty *, caddr_t);
    void (*l_rint)(int, struct tty *);
    void (*l_rend)(struct tty *);
    int (*l_meta)();
    int (*l_start)();
    int (*l_modem)();
} linesw[];
