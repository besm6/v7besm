# task C11 -- the shell carries eight bits.
#
# Everything here is a byte above 0177 going through a part of the shell that used to
# clear bit 0200 out of it.  The text is UTF-8, which is what this machine's console and
# filesystem speak (kernel/dev/sc.c), but nothing below depends on that: the property
# being tested is that the shell is transparent to a BYTE, and the last block proves it
# with 0377 -- the byte the stored form uses as its quoting mark, and so the one that has
# to be doubled rather than carried.
#
# TWO ORACLES.  `set' prints the name tree, which is where most of this lands and which
# needs no external command.  ./echo is the other, and it is the only one that can show
# what reached ARGV -- run-sh-test.sh copies it in beside this script, since a bare `echo'
# under b6sim would find the host's.
#
# NOT HERE, because b6sim cannot reach them: filename generation (it reads a host
# directory with the guest's struct direct) and anything needing a real pipe.
# kernel/test/utils.sh has those, on the image, under the kernel.

# ---- 1. a bare word, and the three ways of quoting one.

plain=привет
double="привет"
single='привет'
escaped=\п\р\и\в\е\т

# ---- 2. substitution: the value goes out and comes back.

joined=$plain-$double
inquotes="один $plain два"
braced=${plain}тся
defaulted=${nosuch-запасной}
assigned=${alsonosuch=сохранённый}
copied=$alsonosuch

# ---- 3. a dollar that must NOT be substituted, each way of saying so.

literal='$plain'
backslashed=\$plain

# ---- 4. word splitting.  IFS is space, tab and newline, and none of them is a byte of
# any of these, so an unquoted Cyrillic value is still one word -- and a quoted space is
# still not a separator.

for w in альфа бета гамма
do	loop=$loop-$w
done

spaced="один   два"
gap=один\ два

# ---- 5. the positional parameters, which utf8.args supplies.

first=$1
count=$#
star="$*"

# ---- 6. `case', which is the pattern matcher without the file system.
#
# NOTE WHAT ? DOES.  The pattern language matches BYTES, so `?' is one byte and not one
# character -- `приве?' does not match `привет', whose last letter is two bytes.  That is
# the honest reading of an eight-bit-clean v7 globber and it is asserted, not worked
# around, so that a later change to it cannot pass unnoticed.

case $plain in
при*)	star_head=yes ;;
*)	star_head=no ;;
esac

case $plain in
*вет)	star_tail=yes ;;
*)	star_tail=no ;;
esac

case $plain in
приве?)	one_byte=yes ;;
*)	one_byte=no ;;
esac

case $plain in
[пм]*)	class=yes ;;
*)	class=no ;;
esac

# A quoted metacharacter is a literal one.  Both arms are here because the interesting
# answer is the second: `a\*b' must NOT match `axb'.

case a*b in
a\*b)	quoted_star=yes ;;
*)	quoted_star=no ;;
esac

case axb in
a\*b)	quoted_star_wild=yes ;;
*)	quoted_star_wild=no ;;
esac

# ---- 7. here-documents.  The unquoted one is read back through subst(), which is where
# the temp file's own copy of the quoting marks is decided; the quoted terminator turns
# that off and the body reaches the command as it was written.

(
read one
read two
set
) <<EOF
первый $plain
второй \$plain
EOF

(
read raw
set
) <<\EOF
третий $plain
EOF

# ---- 8. command substitution, including one that spans two lines.

sub=`./echo привет мир`
subq="`./echo раз; ./echo два`"

# ---- 9. what reaches argv.  The four forms again, this time out of a real exec.

./echo $plain
./echo "$plain"
./echo 'привет $plain'
./echo \п\р\и\в\е\т
./echo один два три
./echo "$star"

# ---- 10. the byte 0377 itself.  It is the shell's own quoting mark in the stored form
# and the only byte that has to be written twice to survive, so it gets its own block.
# It is not valid UTF-8 and the console will not carry it, but a script on the disk can.

ffplain=a�b
ffsingle='a�b'
ffdouble="a�b"
ffesc=a\�b
./echo a�b
./echo "$ffsingle"

# ---- 11. the empty quoted word.  It has to reach argv as an argument rather than vanish,
# and the stored form spells it as a lone QESC with nothing after it -- which every decoder
# has to recognise, since a mark with no payload is otherwise a mark with the terminator
# for a payload.  Both lines below print `a', two spaces and `b'.

empty=""
./echo a "" b
./echo a "$nosuch" b

set

exit 0
