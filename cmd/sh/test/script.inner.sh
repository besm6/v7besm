# run by script.sh as a separate process
# exec fails with ENOEXEC on a file that is not a BESM-6 a.out, and the shell
# then reads it as a script itself - the v7 mechanism, since no shebang exists
# on this system (a `#!' line is now a comment, not a command that is not found)
inner=ran
arg1=$1
arg2=$2
argc=$#
name=$0
set
exit 7
