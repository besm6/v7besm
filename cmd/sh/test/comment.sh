#!/bin/sh
# comment.sh -- the `#' comment character, which v7 had not got.
#
# The line above is the point of the whole test: a `#!' line is now a comment and
# not a command that is not found.  See ../README.md for what does run a script here.
#
# There is NO echo on this system under b6sim -- no external program can be exec'd --
# so, as in the other fixtures, what a case produced is left in a variable and `set'
# dumps the tree at the end.
#
# NOTE: this file deliberately ends WITHOUT a final newline, in a comment.  readc()
# returns SHEOF forever once the input is spent, so the skip in word() has to stop on
# it as well as on the newline; without that guard this test hangs rather than fails.

umask 022

	# an indented comment -- a tab above, spaces below
    # and a bare hash on the next line
#

# The here-document goes FIRST, so that the `set' inside the subshell has a small
# tree to print.  A built-in cannot be redirected but a subshell can, which is the
# same trick heredoc.sh is built on.  The comment sits after the redirection, so the
# newline that ends it is also what flushes the pending document.
(
read h1
read h2
set
) <<EOF	# the document itself starts on the next line
one # not a comment: this is document text
two
EOF

lit=a#b			# not a comment: a hash inside a word is an ordinary character
sq='a # b'		# nor inside single quotes
dq="a # b"		# nor inside double quotes
esc=a\#b		# nor after a backslash
argc=$#			# and $# is still the argument count

semi=one; semi2=two	# after a semicolon, and after the command

# A comment inside `` : the closing backquote is found by a RAW scan first, and only
# then is the text re-parsed -- where the hash comments out the rest of it.  The proof
# is the 0022 above: `umask' with NO argument prints, `umask' with one is silent and
# sets (xec.c, SYSUMASK).  The variable stays empty because b6sim does not capture the
# substitution's pipe -- a limit of that harness, not of the shell, and not new here.
sub=`umask # this text is a comment when the substitution is re-parsed`

bs=before		# this comment ends in a backslash \
bsafter=after		# which must NOT have continued it -- readc, not nextc

set
# a comment on the last line, with no newline after it