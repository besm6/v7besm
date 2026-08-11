{ a[NR] = $1 }
END { for (i = 1; i <= NR; i++) print i, a[i] }
