: run by script.sh as a separate process
: exec fails with ENOEXEC on a file that is not a BESM-6 a.out, and the shell
: then reads it as a script itself - the v7 mechanism, since no shebang exists
: on this system.  no parens, backquotes or semicolons in a colon line: the v7
: shell has no comments, so these words are really parsed
inner=ran
arg1=$1
arg2=$2
argc=$#
name=$0
set
exit 7
