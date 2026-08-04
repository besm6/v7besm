//
// rewinddir(dirp) -- start the directory again from the beginning.
//
// 4.2BSD makes this a macro over seekdir(); it is a function here so that &rewinddir
// exists and so that <dirent.h> can define no macro at all -- b6cpp rejects a macro
// redefinition that is not character-identical, and the way to be safe from that is to
// have none to redefine.  Three words is the whole cost.
//
#include "dirdesc.h"
#include <dirent.h>

void rewinddir(DIR *dirp)
{
    seekdir(dirp, 0L);
}
