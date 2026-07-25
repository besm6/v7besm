: fork and exec of another shell script
outer=start

: 1 - run it by an explicit path, with arguments
./script.inner.sh alpha beta
status=$?

: 2 - find it on PATH instead.  the default PATH is :/bin:/usr/bin and its
: leading empty entry means the current directory
script.inner.sh gamma
pathstatus=$?

: 3 - the child is a full shell, so it can fork in turn
./script.deep.sh
deepouter=$?

set
