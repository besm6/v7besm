# the other half of task C29: a pipeline of four or more stages INSIDE a command
# substitution.  it was a known limit of its own -- task C8 found it, called it
# SIGNAL 4 and left it undiagnosed -- and it was the same machine stack running
# out, macro -> subst -> execute nesting on top of the substitution's own frames.
# ./echo is the one binary the runner brings along, and it ignores its input, so
# what comes back is the last stage's word
two=`./echo a | ./echo b`
three=`./echo a | ./echo b | ./echo c`
four=`./echo a | ./echo b | ./echo c | ./echo d`
five=`./echo a | ./echo b | ./echo c | ./echo d | ./echo e`
set
