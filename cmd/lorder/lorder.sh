
NM=${NM:-b6nm}
prog=$(basename "$0")
trap "rm -f $$sym?ef; exit" 0 1 2 13 15
case $# in
0)	echo "Usage:"
	echo "    $prog file ..."
	echo "Emit dependency pairs among object files and archives, for tsort(1)."
	exit 1 ;;
1)	case $1 in
	*.o)	set $1 $1
	esac
esac
$NM -g $* | sed '
	/^$/d
	/:$/{
		/\.o:/!d
		s/://
		h
		s/.*/& &/
		p
		d
	}
	/[TDL] /{
		s/.* //
		G
		s/\n/ /
		w '$$symdef'
		d
	}
	s/.* //
	G
	s/\n/ /
	w '$$symref'
	d
'
sort $$symdef -o $$symdef
sort $$symref -o $$symref
join $$symref $$symdef | sed 's/[^ ]* *//'
