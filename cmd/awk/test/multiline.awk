BEGIN { RS = "" }
{ print NR ":" $1 "/" $2 }
