$0 ~ /a/ { print "y", $0 }
$0 !~ /a/ { print "n", $0 }
