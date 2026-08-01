# /etc/filters -- the thirteen text filters of task C5, run by the console shell.
#
# Grafted onto a copy of the image by run-filters.sh, not shipped on it.  utils.sh is the
# model and its standing rules hold here too:
#
# WHAT MAY REACH THE LOG: names, diagnostics and numbers a program computed.  Nothing the
# clock decided, no `ls -l', no `date', no pid.  Everything below is a function of the
# fixtures this script writes for itself, so run-filters.sh masks NOTHING -- which is worth
# saying out loud, because utils.sh had to mask two things and this one has to mask none.
#
# WHY THIS TEST EXISTS.  Task C5a put six filters on the image and asserted them under b6sim
# ALONE, deliberately, and cmd/README.md recorded the deferral rather than leaving it to be
# discovered.  cmd/README.md §9 says to test in both worlds where a program runs in both, and
# one two-minute boot was not worth taking for six programs that touch no device, no
# directory, no signal and no second process.  It is worth taking for THIRTEEN, and this is
# it: C5a's six and C5b's seven in a single boot.
#
# FIVE THINGS HERE CANNOT BE SAID UNDER b6sim AT ALL, and they are the reason this is not
# merely a second opinion:
#
#   * look(1) AGAINST ITS DEFAULT DICTIONARY.  look's default is the absolute path
#     /usr/dict/words, and under b6sim that path is the BUILD MACHINE's -- on macOS it does
#     not exist and on Linux it is a 25,000-word file nothing here chose.  Every case in
#     cmd/look/test therefore names its dictionary explicitly, and the bare `look word' form,
#     which is the form look mostly exists in, is asserted HERE and nowhere else.  It is the
#     purest instance of §9's rule about a fixed absolute path there is.
#
#   * tail(1) ON A PIPE.  tail probes descriptor 0 with lseek and branches on ESPIPE: a
#     seekable descriptor is seeked, a pipe is read and discarded.  Under b6sim standard
#     input is always a FILE -- run-prog-test.sh redirects one -- so the pipe half of that
#     branch is unreachable there.  Three of the commands below reach it.
#
#   * col(1)'s TWO DIAGNOSTICS.  col names itself through argv[0], and under b6sim argv[0] is
#     the staged path of the program -- an absolute build directory, which cannot go into a
#     checked-in .expected.  The shell here passes the word as it was typed (cmd/sh's execs()
#     hands exece() the original t[0]), so the diagnostics read `col:' and can be diffed.
#
#   * THE PIPELINES.  Six of the commands below are pipelines between two of these filters,
#     which is the way anybody actually uses them and which no single-program harness can
#     represent.
#
#   * AND THAT THE IMAGE'S COPY RUNS AT ALL.  For C5a's six that is most of what this adds:
#     their logic is fully covered by ctest -L cmd, and what was not covered is that the
#     bytes on the disk execute.  The two `ls /bin' listings assert those files EXIST.
#
# SECTION MARKERS: mount.sh's convention rather than utils.sh's, because thirteen programs in
# one log want a diff that says which one moved.

echo filters begin >/tmp/filters.log

# ---- 0.  THE CORPUS.  Everything below is computed from these four files and nothing else,
#          so the whole log is a function of this script.  They are written with here-
#          documents, which task C3 proved work under the booted kernel, and the delimiter is
#          quoted (`<<\!') so the shell leaves the text alone.
cat >/tmp/f.txt <<\!
alpha
alpha
beta
gamma
gamma
gamma
delta
!
cat >/tmp/a.txt <<\!
apple
banana
cherry
date
!
cat >/tmp/b.txt <<\!
banana
cherry
elder
fig
!
cat >/tmp/n.txt <<\!
1
2
3
4
5
6
7
8
9
10
!

# ---- 1.  tr.  The image's copy folding case, squeezing runs, complementing a set -- and
#          carrying a Cyrillic line through untouched, which is what its 256-entry tables buy
#          and what every other filter in C5b had to have taken out of it.
echo ---tr--- >>/tmp/filters.log
tr a-z A-Z </tmp/f.txt >>/tmp/filters.log
echo привет мир | tr a-z A-Z >>/tmp/filters.log
echo aabbcc | tr -s a-z a-z >>/tmp/filters.log
echo a1b2c3 | tr -cd 0-9 >>/tmp/filters.log
echo >>/tmp/filters.log

# ---- 2.  uniq, including the two-file form -- the one shape of it whose answer is a FILE
#          rather than a stream, and here it is the image's copy creating one.
echo ---uniq--- >>/tmp/filters.log
uniq /tmp/f.txt >>/tmp/filters.log
uniq -c /tmp/f.txt >>/tmp/filters.log
uniq -d /tmp/f.txt >>/tmp/filters.log
uniq -u /tmp/f.txt >>/tmp/filters.log
uniq /tmp/f.txt /tmp/u.txt
cat /tmp/u.txt >>/tmp/filters.log

# ---- 3.  comm, whose collating order is this machine's byte order.
echo ---comm--- >>/tmp/filters.log
comm /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
comm -12 /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
comm -23 /tmp/a.txt /tmp/b.txt >>/tmp/filters.log

# ---- 4.  tail, AND THE PIPE.  The first three go at a seekable file and take lseek; the last
#          three go at a pipe, where the ESPIPE probe fires and tail reads and discards
#          instead.  That second branch is unreachable under b6sim, standard input there
#          always being a file -- see the header.
echo ---tail--- >>/tmp/filters.log
tail -3 /tmp/n.txt >>/tmp/filters.log
tail +8 /tmp/n.txt >>/tmp/filters.log
tail -3r /tmp/n.txt >>/tmp/filters.log
cat /tmp/n.txt | tail -3 >>/tmp/filters.log
cat /tmp/n.txt | tail +8 >>/tmp/filters.log
cat /tmp/n.txt | tail -3c >>/tmp/filters.log

# ---- 5.  look, AGAINST /usr/dict/words.  The default path, which is the whole reason the
#          dictionary is on the image and the one assertion about look that b6sim is barred
#          from making.  `го' finds the Cyrillic entries, which is the -d/-f rule for a byte
#          above 0177 stated as an output.
echo ---look--- >>/tmp/filters.log
look apple >>/tmp/filters.log
look comp >>/tmp/filters.log
look го >>/tmp/filters.log
look zzz >>/tmp/filters.log
echo look status $? >>/tmp/filters.log

# ---- 6.  col, and its two diagnostics.  A reverse line feed (013) backs up over two lines
#          and the two come back merged; a Cyrillic line comes back whole, where v7's col
#          would have printed `P?QP8P2P5Q'; and the bad-option messages name the program
#          through argv[0], which is `col' here and an absolute build path under b6sim.
#
#          THE 013 IS MADE WITH tr(1), because there is no printf(1) on this image and echo(1)
#          is v7's.  A here-document holding a literal vertical tab would be a control
#          character in a checked-in script that nothing can see; `tr X \013' says what it is.
echo ---col--- >>/tmp/filters.log
cat >/tmp/col.in <<\!
AAA
X    BBB
!
tr X '\013' </tmp/col.in >/tmp/col2.in
col </tmp/col2.in >>/tmp/filters.log
echo привет | col >>/tmp/filters.log
col foo >>/tmp/filters.log 2>&1
echo col status $? >>/tmp/filters.log
col -q >>/tmp/filters.log 2>&1
echo col status $? >>/tmp/filters.log

# ---- 7.  od, over bytes the guest itself wrote.  This is the byte order asserted end to end:
#          echo(1) puts the characters on the disk, od reads them back as 48-bit words most
#          significant byte first, and the -c and -b views of the same line have to agree with
#          the -o view about which byte is which.
echo ---od--- >>/tmp/filters.log
echo Hello, BESM-6 world! >/tmp/od.in
od /tmp/od.in >>/tmp/filters.log
od -c /tmp/od.in >>/tmp/filters.log
od -b /tmp/od.in >>/tmp/filters.log
od -w /tmp/od.in >>/tmp/filters.log

# ---- 8.  TASK C5a's SIX, at last on the machine they were built for.  Their logic is covered
#          by ctest -L cmd; what this adds is that the image's copy executes.  Two of them are
#          worth more than that: wc counts a Cyrillic line as two words, which is C5a's own
#          divergence from v7, and rev reverses SEQUENCES rather than bytes, which is the
#          other one.
echo ---c5a--- >>/tmp/filters.log
wc /tmp/f.txt >>/tmp/filters.log
echo привет мир | wc >>/tmp/filters.log
cmp /tmp/f.txt /tmp/f.txt >>/tmp/filters.log
echo cmp status $? >>/tmp/filters.log
cmp /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
echo cmp status $? >>/tmp/filters.log
sum /tmp/f.txt >>/tmp/filters.log
echo hello | tee /tmp/t1 /tmp/t2 >>/tmp/filters.log
cat /tmp/t1 /tmp/t2 >>/tmp/filters.log
rev /tmp/a.txt >>/tmp/filters.log
echo привет | rev >>/tmp/filters.log
split -3 /tmp/f.txt /tmp/x
cat /tmp/xaa /tmp/xab /tmp/xac >>/tmp/filters.log

# ---- 9.  THE FILTERS AGAINST EACH OTHER, which is how anybody uses them and which no
#          single-program harness can represent.  Every one of these is a pipeline between
#          two programs of task C5.
echo ---pipes--- >>/tmp/filters.log
tr a-z A-Z </tmp/f.txt | uniq >>/tmp/filters.log
uniq -c /tmp/f.txt | tr -s ' ' ' ' >>/tmp/filters.log
tail -4 /tmp/f.txt | rev >>/tmp/filters.log
look comp | wc -l >>/tmp/filters.log
comm -12 /tmp/a.txt /tmp/b.txt | od -c >>/tmp/filters.log

echo ---end--- >>/tmp/filters.log
sync
echo filters done >/dev/console
