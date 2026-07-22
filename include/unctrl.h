// unctrl.h
//
// 1/26/81 (Berkeley) @(#)unctrl.h	1.1

#ifndef _UNCTRL_H
#define _UNCTRL_H

extern char *_unctrl[];

#define unctrl(ch) (_unctrl[(unsigned)ch])

#endif // _UNCTRL_H
