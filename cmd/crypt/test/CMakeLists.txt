# b6sim cases for crypt(1) -- task C19.  Each runs the program AS STAGED FOR THE IMAGE with
# the key in <case>.args, feeds it <case>.in and diffs the result against <case>.expected.
#
# THE FIXTURES ARE A SECOND IMPLEMENTATION'S, not this program's own output: ../README.md
# ("The oracle") has the reference and how to regenerate it.  It runs v7's rotor over a real
# 32-bit long and a real 16-bit unsigned on the build machine, so these bytes say the
# WRAP32 emulation is right, not merely that the program is self-consistent.
#
# enc.expected and dec.in are the same 3,100 bytes, and so are dec.expected and enc.in --
# the machine is an involution, so the pair is one round trip written down twice.  Keep them
# byte-identical.
#
# WHY 3,100 BYTES.  It is over BUFSIZ (3,072), so the second fread() runs and crblock() is
# handed a non-zero stream offset -- the one piece of state this port carries between calls
# that v7 kept in two locals.  The content is ../README.md §11's assertion: Cyrillic, then
# every one of the 256 byte values, then filler.  A masked byte does not vanish, it comes
# back as junk, so nothing but a case with a known answer would say so.
b6_progtest(crypt enc)
b6_progtest(crypt dec)

# Only eight characters of the key are used, so these two keys are one key.  The assertion
# is that key8.expected and keylong.expected are byte-identical.
b6_progtest(crypt key8)
b6_progtest(crypt keylong)
