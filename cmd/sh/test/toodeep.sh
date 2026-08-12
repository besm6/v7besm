# task C29: the other end of the same budget -- a recursion with no bottom must
# be reported rather than run off the top of the machine stack.  `eval' of a
# string that evals itself is execexp -> execute -> execexp for ever, and only
# deepchk() (error.c) can stop it: an overflow here wraps onto word 0 and
# rewrites the shell's own image instead of faulting
x='eval $x'
eval $x
unreached=yes
