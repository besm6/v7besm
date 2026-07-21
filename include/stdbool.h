/*
 * <stdbool.h> — boolean type and values (C11 §7.18), BESM-6 target.
 *
 * _Bool occupies one word; only bit 1 is significant (0 = false, 1 = true).
 */
#ifndef _STDBOOL_H
#define _STDBOOL_H

#define bool  _Bool
#define true  1
#define false 0

#define __bool_true_false_are_defined 1

#endif /* _STDBOOL_H */
