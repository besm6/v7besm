BEGIN { a["k"] = 1 }
END { for (v in a) print "key", v }
