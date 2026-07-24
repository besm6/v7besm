#!/bin/sh
# Assert that a native BESM-6 user program fits the address space it will run in.
# Invoked by b6_prog() (scripts/BesmCross.cmake) as one ctest per program:
#     check-size.sh B6SIZE B6NM PROG MAXWORDS MAXADDR
#
# Two independent ceilings, both from doc/Memory_Mapping.md and kernel/TODO.md:
#
#   MAXWORDS -- const+text+data+bss must fit the pages the kernel maps for user text
#     and data.  The user address space is 32 pages of 1 Kword; the top four are the
#     stack, based at 070000, so the image gets 28 -- 28672 words.
#
#   MAXADDR -- no symbol may sit above the reach of a pointer.  A C pointer here
#     carries a 15-bit word address, so word 32767 is the last one nameable.  Only
#     RELOCATABLE symbols are checked: b6nm also prints absolute ones (class `a'),
#     and crt0's ARGC/ARGV are deliberately up at 070000 in the stack page.
#
# Both are silent failures if unchecked -- the link succeeds and the program only
# misbehaves once running -- which is why this is a test and not a comment.
set -e
size=$1
nm=$2
prog=$3
maxwords=$4
maxaddr=$5

# b6size -w prints a header line, then one tab-separated row:
#   const  text  data  bss  dec  oct  name
words=$("$size" -w "$prog" | awk 'NR == 2 { print $5 }')
if [ -z "$words" ]; then
    echo "check-size: $prog: cannot read a size from $size -w" >&2
    exit 1
fi
echo "$prog: $words words of $maxwords (const+text+data+bss)"
if [ "$words" -gt "$maxwords" ]; then
    echo "check-size: $prog: $words words exceeds the $maxwords-word user image space" >&2
    exit 1
fi

# b6nm -n sorts by value; keep the relocatable classes only and take the highest.
top=$("$nm" -n "$prog" | awk '$2 ~ /^[TtDdBbCc]$/ { addr = $1 } END { print addr }')
if [ -n "$top" ]; then
    top=$(printf '%d' "0$top") # b6nm prints octal, with no leading 0
    echo "$prog: highest relocatable symbol at word $top of $maxaddr"
    if [ "$top" -gt "$maxaddr" ]; then
        echo "check-size: $prog: symbol at word $top is past the $maxaddr-word pointer reach" >&2
        exit 1
    fi
fi
