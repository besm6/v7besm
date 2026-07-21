/*
 * <stdalign.h> — alignment (C11 §7.15), BESM-6 target.
 *
 * BESM-6 is word-addressed; every type is 1-word aligned, so alignment is
 * largely a formality here, but the keywords are provided for portability.
 */
#ifndef _STDALIGN_H
#define _STDALIGN_H

#define alignas _Alignas
#define alignof _Alignof

#define __alignas_is_defined 1
#define __alignof_is_defined 1

#endif /* _STDALIGN_H */
