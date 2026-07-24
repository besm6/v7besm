: here-documents - the path through subst in macro.c and copy in io.c
: a built-in cannot be redirected, but a subshell can, and the built-ins
: inside it still print - which is what makes this testable under b6sim
: no parens or backquotes in these lines: the v7 shell has no comments,
: so a colon line is a real command and its words are really parsed

v=EXPANDED

: 1 - an ordinary here-document has dollar substituted in it
(
read one
read two
set
) <<EOF
first $v line
second line
EOF

: 2 - a QUOTED terminator makes it literal, so the dollar stays as written
(
read raw
set
) <<\EOF
first $v line
EOF

: 3 - longer than CPYSIZ, so subst flushes part way through and the
: boundary has to fall in the right place
(
while read line
do last=$line
done
set
) <<EOF
line000 $v padding padding padding
line001 $v padding padding padding
line002 $v padding padding padding
line003 $v padding padding padding
line004 $v padding padding padding
line005 $v padding padding padding
line006 $v padding padding padding
line007 $v padding padding padding
line008 $v padding padding padding
line009 $v padding padding padding
line010 $v padding padding padding
line011 $v padding padding padding
line012 $v padding padding padding
line013 $v padding padding padding
line014 $v padding padding padding
line015 $v padding padding padding
line016 $v padding padding padding
line017 $v padding padding padding
line018 $v padding padding padding
line019 $v padding padding padding
line020 $v padding padding padding
line021 $v padding padding padding
line022 $v padding padding padding
line023 $v padding padding padding
line024 $v padding padding padding
line025 $v padding padding padding
line026 $v padding padding padding
line027 $v padding padding padding
line028 $v padding padding padding
line029 $v padding padding padding
line030 $v padding padding padding
line031 $v padding padding padding
line032 $v padding padding padding
line033 $v padding padding padding
line034 $v padding padding padding
line035 $v padding padding padding
line036 $v padding padding padding
line037 $v padding padding padding
line038 $v padding padding padding
line039 $v padding padding padding
line040 $v padding padding padding
line041 $v padding padding padding
line042 $v padding padding padding
line043 $v padding padding padding
line044 $v padding padding padding
line045 $v padding padding padding
line046 $v padding padding padding
line047 $v padding padding padding
line048 $v padding padding padding
line049 $v padding padding padding
line050 $v padding padding padding
line051 $v padding padding padding
line052 $v padding padding padding
line053 $v padding padding padding
line054 $v padding padding padding
line055 $v padding padding padding
line056 $v padding padding padding
line057 $v padding padding padding
line058 $v padding padding padding
line059 $v padding padding padding
line060 $v padding padding padding
line061 $v padding padding padding
line062 $v padding padding padding
line063 $v padding padding padding
line064 $v padding padding padding
line065 $v padding padding padding
line066 $v padding padding padding
line067 $v padding padding padding
line068 $v padding padding padding
line069 $v padding padding padding
line070 $v padding padding padding
line071 $v padding padding padding
line072 $v padding padding padding
line073 $v padding padding padding
line074 $v padding padding padding
line075 $v padding padding padding
line076 $v padding padding padding
line077 $v padding padding padding
line078 $v padding padding padding
line079 $v padding padding padding
line080 $v padding padding padding
line081 $v padding padding padding
line082 $v padding padding padding
line083 $v padding padding padding
line084 $v padding padding padding
line085 $v padding padding padding
line086 $v padding padding padding
line087 $v padding padding padding
line088 $v padding padding padding
line089 $v padding padding padding
line090 $v padding padding padding
line091 $v padding padding padding
line092 $v padding padding padding
line093 $v padding padding padding
line094 $v padding padding padding
line095 $v padding padding padding
line096 $v padding padding padding
line097 $v padding padding padding
line098 $v padding padding padding
line099 $v padding padding padding
EOF
