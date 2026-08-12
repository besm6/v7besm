# UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.
#
# /usr/bin/calendar -- the reminder service.  Task C23; README.md beside this file.
# No `#!' line and it wants none; ${name-word} has no colon.
#
# `if' AND NOT v7's `case $# in 0) ... *) ... esac', WHICH CRASHES THIS SHELL.  One
# nesting level is the whole difference and README.md is the account.
PATH=/bin:/usr/bin
lib=${LIBCAL-/usr/lib/calendar}
dir=${CALDIR-/usr/lib/calendars}
tmp=/tmp/cal$$
trap "rm -f $tmp; exit" 0 1 2 13 15
$lib >$tmp
if test $# = 0
then
	# The system database is the fallback; -h stops egrep naming the file.
	if test -r calendar
	then	egrep -f $tmp calendar
	else	egrep -h -f $tmp $dir/*
	fi
	exit
fi

# v7's full service, and deliberately without that fallback.
sed 's/\([^:]*\):.*:\(.*\):[^:]*$/y=\2 z=\1/' /etc/passwd |
while read x
do
	eval $x
	if test -r $y/calendar
	then	egrep -f $tmp $y/calendar 2>/dev/null | mail $z
	fi
done
