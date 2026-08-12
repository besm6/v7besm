# task C29: a deeply nested script must run rather than eat the shell
# twelve levels of case/while/if/for, where eight used to be the whole machine
# stack -- and the ninth was silent corruption, not a diagnostic.  none of these
# needs a command, so the whole thing runs under b6sim
outer=start
case one in
one)	if depth=1
	then	for a in x
		do	case two in
			two)	if depth=2
				then	for b in y
					do	case three in
						three)	if depth=3
							then	for c in z
								do	case four in
									four)	if depth=4
										then	for d in w
											do	depth=twelve
											done
										fi
										;;
									esac
								done
							fi
							;;
						esac
					done
				fi
				;;
			esac
		done
	fi
	;;
esac
set
