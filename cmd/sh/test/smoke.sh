# the shell's own smoke test: sourcing, substitution, quoting, the control
# structures, the built-ins and the exit status.  comment.sh covers the hash
# itself; here it is just how a comment is written

. ./smoke.inc.sh

a=one
b=two
joined=${a}-${b}
defaulted=${nosuch-fallback}
replaced=${a+present}
assigned=${alsonosuch=stored}

single='raw $a and no substitution'
double="cooked $a"
escaped=\$a
spaced="one   two"

argv0=$0
argc=$#
first=$1
star=$*
shift
aftershift=$1
setflags=$-

if ifprobe=taken
then	branch=then
else	branch=else
fi

for i in p q r
do	loop=$loop$i
done

case $loop in
pqr)	matched=case-pqr ;;
*)	matched=case-other ;;
esac

whilecount=0
while whilecount=stopped
do	break
done

until untilprobe=set
do	unreached=yes
done

: ${nosuch-quiet}
status=$?

readonly assigned
export a b
export
readonly
umask 022
umask
trap
set

exit 3
