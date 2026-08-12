//
// The one-rotor machine, shared by crypt(1) and ed(1)'s -x.  v7 wrote it twice; one copy
// is what makes crypt.1's "compatible with ed" a property of the build.  See ./README.md.
//
#ifndef _ROTOR_H
#define _ROTOR_H

// Three 256-byte permutations end to end: t1, its inverse t2, and the reflector t3.
#define ROTORSZ 256
#define PERMSZ  (3 * ROTORSZ)

// Derive the rotor from the first eight characters of key; return whether the key was
// non-empty (ed's kflag).  The rotor is derived either way.  DESTROYS key in place.
int crinit(char *key, char *perm);

// Run the machine over nchar bytes in place; startn is buf[0]'s offset in the stream, and
// is the whole of the state between calls.  An involution: twice over is the identity.
void crblock(char *perm, char *buf, int nchar, long startn);

#endif // _ROTOR_H
