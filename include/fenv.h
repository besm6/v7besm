// <fenv.h> -- floating-point environment (C11 §7.6), BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/fenv.h.
//
// The environment is degenerate, and honestly so.  This machine has no IEEE
// sticky exception flags, no rounding-mode register and no floating-point traps
// a program can arm: there is one mode and nothing to read back.  So
// FE_ALL_EXCEPT is 0 -- §7.6p6 defines it as the bitwise OR of the exception
// macros an implementation defines, and this one defines none -- and the only
// rounding direction is FE_TONEAREST.  Every function below then has an exact
// and conforming answer to give: the exception routines succeed having done
// nothing, fegetround always says FE_TONEAREST, and fesetround succeeds only for
// that value.
//
// This is the reason there is no <math.h> INFINITY or NAN either: there is
// nothing in the format to raise a flag about.
//
// TODO: the eleven routines, in libc.  Each is two or three instructions.
#ifndef _FENV_H
#define _FENV_H

typedef int fexcept_t;

typedef struct {
    int __mode;
} fenv_t;

// No exception flags exist, so the OR of them is empty (§7.6p6).
#define FE_ALL_EXCEPT 0

// The single rounding direction.
#define FE_TONEAREST 0

#define FE_DFL_ENV ((const fenv_t *)0)

// ---- declared for future implementation (TODO) ----
int feclearexcept(int excepts);
int fetestexcept(int excepts);
int feraiseexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);

int fegetround(void);
int fesetround(int round);

int fegetenv(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feholdexcept(fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#endif // _FENV_H
