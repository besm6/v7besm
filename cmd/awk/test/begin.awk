BEGIN { print "b", NR }
{ print $1 }
