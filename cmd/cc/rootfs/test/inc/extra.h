// Found only through cc's -I, and only when -DEXTRA has been given: the `defines'
// case of the cc agreement suite (task C9e).  It is in a subdirectory of its own
// so that the -I is load-bearing -- a header beside the source would be found by
// the quoted form with no search path at all.
#define EXTRA_WORD 0x606060606060
