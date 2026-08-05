# /etc/filters -- the twenty-four text filters of task C5, run by the console shell, and
# since C25b one program that is not a filter at all: man(1), whose section at the end says
# why it is here rather than in a boot of its own.
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
# it: C5a's six and C5b's seven in a single boot.  Task C5c added grep and fgrep to it, as
# cmd/TODO.md said a later filter should -- a section here rather than a second boot -- and so
# did C5d's sort, C5e's sed and C5f's seven, which takes it to twenty-four.
#
# EIGHT THINGS HERE CANNOT BE SAID UNDER b6sim AT ALL, and they are the reason this is not
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
#   * find(1), ENTIRELY (task C5f).  cmd/find has no test/ directory at all: it reads
#     directory descriptors, which b6sim refuses; it popen()s pwd, which there would fork the
#     BUILD MACHINE's shell; and -exec fork/execs, which there would run the build machine's
#     programs on the build machine's files.  Section 18 is not a second opinion about find,
#     it is the only one -- mount(1M)'s position from task C4f exactly.
#
#   * file(1) ON THE THINGS IT EXISTS TO LOOK AT (task C5f).  A real BESM-6 a.out with FMAGIC
#     and RELFLG set, a pure one, a directory, a block special file and a character special
#     file are all things that exist on the IMAGE; under b6sim /bin, /dev and /etc are the
#     build machine's, so cmd/file/test has to copy build artefacts under neutral names and
#     can say nothing at all about the last four.
#
#   * diff(1)'s TEMP FILE AND ITS -h PATH (task C5f).  `diff -' copies standard input to
#     /tmp/dXXXXX, and `diff -h' EXECs /usr/lib/diffh -- which is on the image and not on the
#     build machine, so cmd/diff/test can only assert the diagnostic.
#
#   * A SEPARATOR, A PATTERN OR A WHOLE SCRIPT WITH A SPACE IN IT (tasks C5c, C5d and C5e).  run-prog-test.sh splits a .args line on
#     whitespace and has no quoting, so no b6sim case can hand grep or fgrep a pattern
#     containing a space -- which is a large part of what anybody greps for, and `sort -t\' \''
#     is the same limit on the commonest field separator there is.  Here the shell quotes
#     them, so `grep 'вет мир'', `sort -t' '' and `sed 's/привет мир/мир привет/'' are
#     single arguments and the cases exist.  sed is the largest instance of the three: a
#     substitution whose pattern or replacement holds a space is most of what sed is for.
#
#   * A TEMP FILE, OR A w FILE, IN THE IMAGE'S OWN /tmp (tasks C5d and C5e).  sort makes one before it reads
#     anything and unlinks it on the way out; under b6sim that path is the build machine's,
#     so nothing there may assert either half.  Section 10's closing glob asserts both.
#
#   * THE PIPELINES.  Sixteen of the commands below are pipelines between two of these filters,
#     which is the way anybody actually uses them and which no single-program harness can
#     represent.
#
#   * AND THAT THE IMAGE'S COPY RUNS AT ALL.  For C5a's six that is most of what this adds:
#     their logic is fully covered by ctest -L cmd, and what was not covered is that the
#     bytes on the disk execute.  The two `ls /bin' listings assert those files EXIST.
#
# SECTION MARKERS: mount.sh's convention rather than utils.sh's, because twenty-four programs in
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

# ---- 8.  grep (task C5c), AND A PATTERN WITH A SPACE IN IT.  The character class over a
#          Cyrillic byte is the task's subject and ctest -L cmd covers it; what only this
#          test can do is quote a pattern, run the image's copy, and put the answer through a
#          pipe.  -b is the deliberate divergence: a byte offset into the file, and /tmp/f.txt
#          puts `delta' at byte 35 -- fgrep below must say 35 for the same line.
echo ---grep--- >>/tmp/filters.log
grep gamma /tmp/f.txt >>/tmp/filters.log
grep -c a /tmp/f.txt >>/tmp/filters.log
grep -n delta /tmp/f.txt >>/tmp/filters.log
grep -b delta /tmp/f.txt >>/tmp/filters.log
grep -v gamma /tmp/f.txt >>/tmp/filters.log
grep '[bg]' /tmp/f.txt >>/tmp/filters.log
echo привет мир | grep '[пм]' >>/tmp/filters.log
echo привет мир | grep 'вет мир' >>/tmp/filters.log
echo ALPHA | grep -y alpha >>/tmp/filters.log
grep zzz /tmp/f.txt >>/tmp/filters.log
echo grep status $? >>/tmp/filters.log

# ---- 9.  fgrep (task C5c).  The keyword list is newline-separated, so the several-keyword
#          form cannot be a b6sim argument at all and -f is what carries it; here a here-
#          document writes the list, which is the way it is actually used.  -b must agree with
#          grep's above, which is the whole point of taking the divergence in both programs.
echo ---fgrep--- >>/tmp/filters.log
fgrep gamma /tmp/f.txt >>/tmp/filters.log
fgrep -c a /tmp/f.txt >>/tmp/filters.log
fgrep -n delta /tmp/f.txt >>/tmp/filters.log
fgrep -b delta /tmp/f.txt >>/tmp/filters.log
fgrep -x beta /tmp/f.txt >>/tmp/filters.log
cat >/tmp/k.txt <<\!
beta
delta
!
fgrep -f /tmp/k.txt /tmp/f.txt >>/tmp/filters.log
echo привет мир | fgrep 'вет мир' >>/tmp/filters.log
fgrep zzz /tmp/f.txt >>/tmp/filters.log
echo fgrep status $? >>/tmp/filters.log

# ---- 10. sort (task C5d).  Four of these cannot be a b6sim case at all, and one of them is
#          the reason this program wanted the boot more than any other filter has.
#
#          A SEPARATOR THAT IS A SPACE.  run-prog-test.sh splits a .args line on whitespace
#          and has no quoting, so `-t\' \'' is unrepresentable there -- and a space is the
#          commonest separator anybody sorts on.  C5c paid this cost once for a grep pattern;
#          this is the second instance, and between them they are what the limit costs.
#
#          A TEMP FILE IN THE IMAGE'S OWN /tmp.  sort creates /tmp/stm<pid>aa before it reads
#          anything and unlinks it on the way out.  Under b6sim that path is the BUILD
#          MACHINE's, so no case there may say a word about it; here the glob below says
#          both that the machine could make one and that none survived.
#
#          THE IMAGE'S OWN FILES AS INPUT.  /etc/passwd is a build constant and its third
#          colon field is a number, which is exactly sort.1's own example; and
#          /usr/dict/words is the file task C5b staged for look, sorted on the build host
#          with `LC_ALL=C sort -c'.  The guest checking it is the machine confirming, in its
#          own collating order, the ordering the host asserted -- which nothing else here can
#          close.
echo ---sort--- >>/tmp/filters.log
cat >/tmp/s.txt <<\!
pear 3 green
apple 10 red
fig 2 purple
apple 9 red
!
sort /tmp/s.txt >>/tmp/filters.log
sort -t' ' +1n /tmp/s.txt >>/tmp/filters.log
sort +2 -3 /tmp/s.txt >>/tmp/filters.log
sort -u /tmp/f.txt >>/tmp/filters.log
sort -m /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
sort -t: +2n /etc/passwd >>/tmp/filters.log
sort -o /tmp/so.txt /tmp/s.txt
cat /tmp/so.txt >>/tmp/filters.log
sort -c /usr/dict/words
echo sort status $? >>/tmp/filters.log
echo /tmp/stm* >>/tmp/filters.log

# ---- 11. sed (task C5e), AND A SCRIPT WITH SPACES IN IT.  The character class over a
#          Cyrillic byte, the y/// table and the QESC replacement mark are the task's subject
#          and ctest -L cmd covers all three; four things only this test can do.
#
#          A QUOTED SCRIPT.  run-prog-test.sh splits a .args line on whitespace and has no
#          quoting, so no b6sim case can hand sed `s/привет мир/мир привет/' -- and a
#          substitution whose pattern or replacement contains a space is most of what
#          anybody writes sed for.  This is the third instance of that limit after grep's
#          pattern and sort's separator, and the largest.
#
#          A SCRIPT FROM A HERE-DOCUMENT.  sed -f is how a script of more than one line
#          actually arrives, and a here-document is how the shell writes one.
#
#          A w FILE IN THE IMAGE'S OWN /tmp.  Under b6sim that path is the BUILD MACHINE's
#          (README.md SS9's absolute-path rule), so cmd/sed/test writes into its own
#          working directory and can say nothing about /tmp.
#
#          AND THE l COMMAND OVER A CONSOLE THAT SPEAKS UTF-8, which is what the divergence
#          is for: a byte above 0177 is passed through rather than spelled in octal.
echo ---sed--- >>/tmp/filters.log
sed -n 2p /tmp/f.txt >>/tmp/filters.log
sed 's/alpha/ALPHA/' /tmp/f.txt >>/tmp/filters.log
sed -n '/gamma/,/delta/p' /tmp/f.txt >>/tmp/filters.log
sed -n '$=' /tmp/f.txt >>/tmp/filters.log
echo привет мир | sed 's/привет мир/мир привет/' >>/tmp/filters.log
echo привет мир | sed -n '/[пм]/p' >>/tmp/filters.log
echo ALPHA | sed -n '/[п]/p' >>/tmp/filters.log
echo x | sed 's/x/привет мир/' >>/tmp/filters.log
echo мир | sed 'y/мир/МИР/' >>/tmp/filters.log
echo привет | sed -n l >>/tmp/filters.log
sed -n 'w /tmp/sedw.txt' /tmp/a.txt
cat /tmp/sedw.txt >>/tmp/filters.log
cat >/tmp/sed.sed <<\!
s/^/> /
2d
!
sed -f /tmp/sed.sed /tmp/b.txt >>/tmp/filters.log

# ---- 12. pr (task C5f).  Every case in cmd/pr/test passes -t, and it has to: pr's page
#          heading holds the FILE'S MTIME, so no run of pr without -t has a reproducible
#          standard output anywhere -- here least of all, where the clock is the image's own
#          -T stamp plus however long the boot took.  So the heading is asserted under b6sim
#          (cmd/pr/test/run-pr-test.sh fixes an mtime with touch first) and what this section
#          adds is the image's copy running, the -m path that never touches the ring buffer,
#          and a Cyrillic column through that ring -- the two sentinel bytes 0375 and 0376
#          live IN the character stream and survive only because neither can occur in UTF-8.
echo ---pr--- >>/tmp/filters.log
pr -t -l8 /tmp/a.txt >>/tmp/filters.log
pr -t -l4 -2 /tmp/n.txt >>/tmp/filters.log
pr -t -l4 -2 -s, /tmp/n.txt >>/tmp/filters.log
pr -t -l4 -m /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
echo привет мир | pr -t -l2 >>/tmp/filters.log

# ---- 13. cal (task C5f).  The cheapest program of the task and the one output everybody
#          checks: September 1752, the month Britain lost eleven days.
echo ---cal--- >>/tmp/filters.log
cal 9 1752 >>/tmp/filters.log
cal 2 2000 >>/tmp/filters.log
cal 2 1900 >>/tmp/filters.log

# ---- 14. tsort (task C5f), which is lorder(1)'s other half.  A Cyrillic node name is an
#          ordinary item here: getname()'s blank set is written out rather than taken from a
#          table, so a byte above 0177 is a name character.
echo ---tsort--- >>/tmp/filters.log
cat >/tmp/g.txt <<\!
main.o util.o
util.o io.o
main.o io.o
io.o io.o
!
tsort /tmp/g.txt >>/tmp/filters.log
echo 'привет мир' | tsort >>/tmp/filters.log

# ---- 15. join (task C5f).  The many-to-many path -- keys repeated on BOTH sides -- is most
#          of this program, and it is the reason file2 has to be a real file: join rewinds it
#          with fseek once per group of equal keys.
echo ---join--- >>/tmp/filters.log
cat >/tmp/j1.txt <<\!
apple 1
banana 2
cherry 3
!
cat >/tmp/j2.txt <<\!
apple red
banana yellow
banana green
date brown
!
join /tmp/j1.txt /tmp/j2.txt >>/tmp/filters.log
join -a1 /tmp/j1.txt /tmp/j2.txt >>/tmp/filters.log
join -o 1.1 2.2 /tmp/j1.txt /tmp/j2.txt >>/tmp/filters.log

# ---- 16. file (task C5f), AND THIS IS THE SECTION b6sim CANNOT HAVE.  Every interesting
#          input to file is a thing that exists only on the image: a real BESM-6 a.out with
#          FMAGIC and RELFLG set, a directory, a block special file, a character special file,
#          and a shell script whose mode carries an execute bit.  Under b6sim /bin, /dev and
#          /etc are the BUILD MACHINE's (cmd/README.md §9), so cmd/file/test has to copy
#          build artefacts under neutral names and can say nothing about the last four.
#          The Cyrillic line is the divergence: v7 called any byte above 0177 `data'.
echo ---file--- >>/tmp/filters.log
file /bin/cat >>/tmp/filters.log
file /bin/sh >>/tmp/filters.log
file / >>/tmp/filters.log
file /dev/md0 >>/tmp/filters.log
file /dev/console >>/tmp/filters.log
file /etc/passwd >>/tmp/filters.log
file /tmp/a.txt >>/tmp/filters.log
echo привет мир >/tmp/rus.txt
file /tmp/rus.txt >>/tmp/filters.log
file /tmp/nosuch >>/tmp/filters.log

# ---- 17. diff (task C5f), AND THE TWO THINGS ONLY A BOOT CAN SHOW.  `diff -' copies
#          standard input to a temp file in /tmp, and under b6sim that /tmp is the build
#          machine's (§9 again); and `diff -h' EXECs /usr/lib/diffh, which is on the image and
#          not on the build machine, so cmd/diff/test can only assert the diagnostic.  Here
#          both work.  The closing glob says the temp file did not survive.
echo ---diff--- >>/tmp/filters.log
diff /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
echo diff status $? >>/tmp/filters.log
diff /tmp/a.txt /tmp/a.txt >>/tmp/filters.log
echo diff status $? >>/tmp/filters.log
diff -e /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
cat /tmp/a.txt | diff - /tmp/b.txt >>/tmp/filters.log
diff -h /tmp/a.txt /tmp/b.txt >>/tmp/filters.log
echo /tmp/d* >>/tmp/filters.log

# ---- 18. find (task C5f), AND THIS IS THE WHOLE OF WHAT IS ASSERTED ABOUT IT.  cmd/find has
#          no test/ directory at all: it reads directory descriptors, which b6sim refuses;
#          it popen()s pwd, which under b6sim would fork the build machine's shell; and -exec
#          fork/execs, which under b6sim would run the build machine's programs on the build
#          machine's files.  So this section is not a second opinion, it is the only one --
#          mount(1M)'s C4f position exactly, and cmd/find/README.md says so.
#
#          The walk order is a directory's own entry order, so everything below goes through
#          sort: what is being asserted is WHICH files matched, not the order the filesystem
#          happens to hold them in.  And every query is labelled, because a dozen unlabelled
#          lists of the same three names is a log nobody can read a diff of.
echo ---find--- >>/tmp/filters.log
mkdir /tmp/ftree
mkdir /tmp/ftree/sub
echo one >/tmp/ftree/one.c
echo two >/tmp/ftree/two.txt
# A second of daylight between the two halves of the tree, so that -newer below has a real
# answer rather than a race: the guest clock advances about two seconds over a whole boot,
# so two files written one after the other usually carry the SAME mtime.
sleep 2
echo three >/tmp/ftree/sub/three.c
echo 'find -print' >>/tmp/filters.log
find /tmp/ftree -print | sort >>/tmp/filters.log
echo 'find -name' >>/tmp/filters.log
find /tmp/ftree -name '*.c' -print | sort >>/tmp/filters.log
echo 'find -type d' >>/tmp/filters.log
find /tmp/ftree -type d -print | sort >>/tmp/filters.log
echo 'find -type f -name' >>/tmp/filters.log
find /tmp/ftree -type f -name '*.txt' -print | sort >>/tmp/filters.log
echo 'find ( -o )' >>/tmp/filters.log
find /tmp/ftree \( -name '*.c' -o -name '*.txt' \) -print | sort >>/tmp/filters.log
echo 'find ! -type d' >>/tmp/filters.log
find /tmp/ftree ! -type d -print | sort >>/tmp/filters.log
echo 'find -size 1' >>/tmp/filters.log
find /tmp/ftree -size 1 -type f -print | sort >>/tmp/filters.log
echo 'find -links 1' >>/tmp/filters.log
find /tmp/ftree -links 1 -type f -print | sort >>/tmp/filters.log
# chmod first, so that the mode being matched is one this script chose rather than whatever
# the shell's umask happens to be.
chmod 600 /tmp/ftree/one.c
echo 'find -perm 0600' >>/tmp/filters.log
find /tmp/ftree -perm 0600 -type f -print | sort >>/tmp/filters.log
echo 'find -user root' >>/tmp/filters.log
find /tmp/ftree -user root -type f -print | sort >>/tmp/filters.log
echo 'find -newer' >>/tmp/filters.log
find /tmp/ftree -newer /tmp/ftree/two.txt -type f -print | sort >>/tmp/filters.log
echo 'find -exec' >>/tmp/filters.log
find /tmp/ftree -name three.c -exec cat {} \; >>/tmp/filters.log
echo 'find -cpio' >>/tmp/filters.log
find /tmp/ftree -cpio /tmp/nope >>/tmp/filters.log 2>&1

# ---- 19. TASK C5a's SIX, at last on the machine they were built for.  Their logic is covered
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

# ---- 20. THE FILTERS AGAINST EACH OTHER, which is how anybody uses them and which no
#          single-program harness can represent.  Every one of these is a pipeline between
#          two programs of task C5.
echo ---pipes--- >>/tmp/filters.log
tr a-z A-Z </tmp/f.txt | uniq >>/tmp/filters.log
uniq -c /tmp/f.txt | tr -s ' ' ' ' >>/tmp/filters.log
tail -4 /tmp/f.txt | rev >>/tmp/filters.log
look comp | wc -l >>/tmp/filters.log
comm -12 /tmp/a.txt /tmp/b.txt | od -c >>/tmp/filters.log
grep gamma /tmp/f.txt | wc -l >>/tmp/filters.log
uniq /tmp/f.txt | fgrep -c a >>/tmp/filters.log
look comp | grep -c comp >>/tmp/filters.log
cat /tmp/f.txt | sort -u >>/tmp/filters.log
sed -n 's/^/> /p' /tmp/a.txt | wc -l >>/tmp/filters.log
tr a-z A-Z </tmp/b.txt | sed -n '$p' >>/tmp/filters.log
ls /bin | sort -c
echo sort status $? >>/tmp/filters.log
find /tmp/ftree -name '*.c' -print | wc -l >>/tmp/filters.log
cal 1 2026 | pr -t -l9 >>/tmp/filters.log
# THE `sed' IS NOT DECORATION and task C6 is what put it there.  tsort reads its input as
# PAIRS, and v7's says `odd data' and produces nothing at all when there is an odd token left
# over -- so `ls /bin | tsort' silently depended on how many programs happen to be in /bin, and
# went from 54 lines to none the day that number became 61.  Fixing the count here keeps the
# assertion about the PIPE, which is what this section is for, rather than about the size of
# the image.
ls /bin | sed -n '1,40p' | tsort | wc -l >>/tmp/filters.log
file /bin/cat | sed 's/.*	//' >>/tmp/filters.log

# ---- 21. man (task C25b), AND IT IS NOT A FILTER.  It is here rather than in a boot of its
#          own for cmd/README.md §9's arithmetic -- a section costs a section, a boot costs
#          two minutes and a volume number -- and because what it needs is exactly what this
#          script already has: the real /usr/man, a shell, and a pipe.
#
#          THREE THINGS ONLY A BOOT CAN SAY, and they are the whole reason cmd/man/test is
#          not enough.  THE DEFAULT SEARCH PATH: man's is the absolute /usr/man, which under
#          b6sim is the BUILD MACHINE's, so every case there names a fixture tree with -M --
#          look(1) in section 5 above is the same rule and this is its second instance.
#          $MANPATH: run-prog-test.sh runs `env -i'.  And THE DISPLAY ITSELF: with neither
#          -w nor -, man builds one command string and hands it to system(3), which forks
#          /bin/sh -- and b6sim can fork but cannot exec a BESM-6 a.out.  So the two cmp
#          lines below are the only assertion anywhere that man SHOWS a page, and the oracle
#          is the page file rather than a checked-in copy of it: man runs /bin/cat, so the
#          output must be the file byte for byte.  What is still unreachable in both worlds
#          is the `| /bin/more' tail, which is appended only for a terminal -- cmd/more's
#          screen half, with cmd/more's reason and kernel/TODO.md task 35's timing.
echo ---man--- >>/tmp/filters.log
man -w ls >>/tmp/filters.log
man -w 1m fsck >>/tmp/filters.log
man -w 3 stdio >>/tmp/filters.log
man -w 2 mount >>/tmp/filters.log
man -aw mount >>/tmp/filters.log
man -w man >>/tmp/filters.log
MANPATH=/tmp:/usr/man man -w ls >>/tmp/filters.log
man -w nosuchpage >>/tmp/filters.log 2>&1
echo man status $? >>/tmp/filters.log
man ls | cmp - /usr/man/man1/ls.1
echo man cmp status $? >>/tmp/filters.log
man - ls | cmp - /usr/man/man1/ls.1
echo man - cmp status $? >>/tmp/filters.log
man 1m fsck | wc -l >>/tmp/filters.log

echo ---end--- >>/tmp/filters.log
sync
echo filters done >/dev/console
