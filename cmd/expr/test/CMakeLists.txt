# b6sim cases for expr(1) -- task C11.  ../../README.md SS9: a pure filter over its ARGUMENT
# LIST, so this harness is the whole of it.  Each case is an .args line, an .expected and,
# where the answer is null or false, a .status: expr exits 1 for a result of `0' or the null
# string and 2 for a bad expression, so a case with no .status asserts a true answer.
#
# EVERY OPERATOR THIS PROGRAM HAS SURVIVES AN .args LINE, which looks impossible and is
# worth writing down.  run-prog-test.sh expands the line UNQUOTED -- `$args' -- so the shell
# field-splits it but does not re-parse it: `|', `&', `>', `(' and `)' arrive as ordinary
# words.  `set -f' is what makes `*' and `[a-c]' safe, and it is there for task C5c's
# regular expressions already.  Backslashes are not removed either, so `a\(bc\)' reaches the
# program intact.
#
# WHAT CANNOT BE A CASE HERE IS AN EMPTY ARGUMENT, there being no quoting: `expr "" = ""',
# `expr abc : ""' and the null operand of `|' and `&' are unassertable.  Nor is there
# anywhere else for them -- kernel/test/ has three tests left and none of them is a filter.
# The three are exercised by hand and the answers are in ../README.md.
b6_progtest(expr add)
b6_progtest(expr sub)
b6_progtest(expr mul)
b6_progtest(expr div)
b6_progtest(expr rem)
b6_progtest(expr precedence)
b6_progtest(expr parens)
b6_progtest(expr bigword)
b6_progtest(expr negarith)
b6_progtest(expr divzero)
b6_progtest(expr remzero)
b6_progtest(expr nonnumeric)
b6_progtest(expr noargs)
b6_progtest(expr unbalanced)

b6_progtest(expr lt)
b6_progtest(expr le)
b6_progtest(expr lefalse)
b6_progtest(expr ge)
b6_progtest(expr negrel)
b6_progtest(expr numne)
b6_progtest(expr strlt)
b6_progtest(expr strfalse)

b6_progtest(expr orfirst)
b6_progtest(expr orsecond)
b6_progtest(expr orzero)
b6_progtest(expr andzero)
b6_progtest(expr andtrue)

b6_progtest(expr matchcount)
b6_progtest(expr matchfail)
b6_progtest(expr matchgroup)
b6_progtest(expr matchnull)
b6_progtest(expr matchrange)
b6_progtest(expr basename)
b6_progtest(expr basenameplain)
b6_progtest(expr reerror)
b6_progtest(expr twobra)
b6_progtest(expr deep)

b6_progtest(expr matchkw)
b6_progtest(expr substr)
b6_progtest(expr substrzero)
b6_progtest(expr substrpast)
b6_progtest(expr length)
b6_progtest(expr index)
b6_progtest(expr indexnone)
b6_progtest(expr paren3)

b6_progtest(expr utf8arg)
b6_progtest(expr utf8length)
b6_progtest(expr utf8match)
b6_progtest(expr utf8class)
b6_progtest(expr utf8fold)
b6_progtest(expr utf8index)
