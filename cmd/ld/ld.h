//
// Linker for BESM-6 a.out objects.
// Public entry point: drive the linker from argv, so it can be invoked by the
// command-line front end (main.c) and by unit tests alike.  This header is
// intentionally C++-safe so the GoogleTest suite can include it directly.
//
#ifndef BESM6_LD_LD_H
#define BESM6_LD_LD_H

#ifdef __cplusplus
extern "C" {
#endif

// Link the object files named in argv (same option syntax as the b6ld command
// line) and write the executable image.  Returns the error level (0 on
// success).  Unlike a bare main(), it does not call exit() on the success path.
int ld_link(int argc, char **argv);

// Clean up and terminate; installed as the SIGINT/SIGTERM handler by main.c.
void delexit(void);

#ifdef __cplusplus
}
#endif

#endif // BESM6_LD_LD_H
