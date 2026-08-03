    .bss
alloca:
    . = . + 1
    .data
allocb:
    .word alloca
    .data
allocp:
    .word alloca
    .bss
allocx:
    . = . + 1
    .text
    .globl malloc
malloc:
    its 13
 13 vjm b$save
 15 utm 62
  6 xta
    xts #0600000
 13 vjm b$uge
    uza .T13
    xta
    uj b$ret
.T13:
  6 xta
    xts #6
 13 vjm b$uadd
    xts #6
 13 vjm b$uadd
    xts #1
 13 vjm b$usub
    xts #6
 13 vjm b$udiv
  7 atx 5
.T23:
    utc allocp
    xta
  7 atx 6
    utc alloca
    xta
    xts
 13 vjm b$ne
    uza .T27
    xta
  7 atx 9
.T30:
    xta
  7 a+x 6
  7 atx 10
  7 wtc 10
    xta
    aax #0100000
 13 vjm b$not
    uza .T37
.L5:
    xta
  7 a+x 6
  7 atx 14
  7 wtc 14
    xta
  7 atx 15
    xta
  7 a+x 15
  7 atx 16
  7 wtc 16
    xta
    aax #0100000
 13 vjm b$not
    uza .L4
    xta
  7 a+x 15
  7 atx 20
  7 wtc 20
    xta
  7 atx 21
    xta
  7 a+x 6
  7 atx 22
  7 xta 21
  7 wtc 22
    atx
    uj .L5
.L4:
  7 xta 6
  7 a+x 5
  7 atx 23
  7 xta 15
  7 xts 23
 13 vjm b$ge
    uza .T53
  7 xta 6
  7 a+x 5
  7 xts 6
 13 vjm b$ge
    xts
 13 vjm b$ne
  7 atx 27
    uj .T54
.T53:
    xta
  7 atx 27
.T54:
  7 xta 27
    u1a .L61
.T59:
    uj .T38
.T37:
.T38:
  7 xta 6
  7 atx 28
    xta
  7 a+x 6
  7 atx 29
  7 wtc 29
    xta
    aax #037777777677777
  7 atx 31
  7 atx 6
  7 xta 31
  7 xts 28
 13 vjm b$le
    uza .T70
  7 xta 31
    utc allocb
    xts
 13 vjm b$ne
    uza .T73
    xta
    uj b$ret
.T73:
  7 xta 9
    xts #1
 13 vjm b$uadd
  7 atx 34
  7 atx 9
  7 xta 34
    xts #1
 13 vjm b$ugt
    u1a .L2
.T79:
    uj .T71
.T70:
.T71:
    uj .T30
.L2:
    uj .T28
.T27:
.T28:
  7 xta 5
    xts #02000
 13 vjm b$uadd
    asn 74
    asn 54
  7 atx 38
  7 atx 9
    xta
 14 vtm -1
 13 vjm sbrk
    aax #00037777777777777
  7 atx 40
  7 a+x 38
  7 xts 40
 13 vjm b$le
    uza .T92
    xta
    uj b$ret
.T92:
.T95:
  7 xta 9
    xts #6
 13 vjm b$umul
 14 vtm -1
 13 vjm sbrk
    aax #00037777777777777
  7 atx 45
    aox #0'64
    aax #00037777777777777
    xts
 13 vjm b$ne
    u1a .L6
.T105:
  7 xta 9
    xts #02000
 13 vjm b$usub
  7 atx 49
  7 atx 9
  7 xta 49
  7 xts 5
 13 vjm b$ule
    uza .T110
    xta
    uj b$ret
.T110:
    uj .T95
.L6:
  7 xta 45
    aox #0'64
  7 atx 51
  7 xta 9
    xts #6
 13 vjm b$umul
  7 atx 52
  7 xta 51
  7 xts 52
 14 vtm -2
 13 vjm ialloc
    utc allocb
    xta
    utc allocp
    atx
    uj .T23
.L61:
  7 xta 6
  7 a+x 5
  7 atx 53
    utc allocp
    atx
  7 xta 15
  7 xts 53
 13 vjm b$gt
    uza .T120
    xta
  7 a+x 53
  7 atx 55
  7 wtc 55
    xta
    utc allocx
    atx
    xta
  7 a+x 6
  7 atx 57
  7 wtc 57
    xta
  7 atx 58
    xta
  7 a+x 53
  7 atx 59
  7 xta 58
  7 wtc 59
    atx
    uj .T121
.T120:
.T121:
  7 xta 53
    aox #0100000
  7 atx 60
    xta
  7 a+x 6
  7 atx 61
  7 xta 60
  7 wtc 61
    atx
  7 xta 6
    a+x #1
    aox #0'64
    uj b$ret
    .text
    .globl free
free:
    its 13
 13 vjm b$save
 15 utm 8
  6 xta
    aax #00037777777777777
    xts
 13 vjm b$eq
    u1a b$ret
.T139:
  6 xta
    aax #00037777777777777
    a-x #1
  7 atx 3
    utc allocp
    atx
    xta
  7 a+x 3
  7 atx 4
  7 wtc 4
    xta
    aax #037777777677777
  7 atx 6
    xta
  7 a+x 3
  7 atx 7
  7 xta 6
  7 wtc 7
    atx
    uj b$ret
    .text
    .globl ialloc
ialloc:
    its 13
 13 vjm b$save
 15 utm 30
  6 xta
    aax #00037777777777777
  7 atx
  7 atx 1
  6 xta 1
    xts #6
 13 vjm b$udiv
  7 atx 2
  7 xta
  7 a+x 2
    a-x #1
  7 atx 4
  7 atx 5
    xta
  7 a+x
  7 atx 6
  7 xta 4
  7 wtc 6
    atx
    utc alloca
    xta
    xts
 13 vjm b$eq
    uza .T164
 14 vtm alloca
    ita 14
    utc alloca
    atx
    uj .T165
.T164:
.T165:
    utc allocb
    xta
  7 atx 10
.T167:
    xta
  7 a+x 10
  7 atx 11
  7 wtc 11
    xta
  7 atx 12
    xta #0100000
    aex #07777777777777777
  7 atx 13
  7 xta 12
  7 aax 13
  7 atx 14
  7 atx 15
  7 xta 14
    utc allocb
    xts
 13 vjm b$eq
    u1a .L151
.T176:
  7 xta 15
  7 xts 5
 13 vjm b$gt
    uza .T179
  7 xta 10
  7 xts 1
 13 vjm b$lt
    xts
 13 vjm b$ne
  7 atx 19
    uj .T180
.T179:
    xta
  7 atx 19
.T180:
  7 xta 19
    u1a .L151
.T183:
  7 xta 15
  7 atx 10
    uj .T167
.L151:
  7 xta 10
    a+x #1
  7 atx 20
  7 xta 1
  7 xts 20
 13 vjm b$eq
    uza .T188
  7 xta 1
  7 atx 22
    uj .T189
.T188:
  7 xta 1
    aox #0100000
  7 atx 22
.T189:
    xta
  7 a+x 10
  7 atx 24
  7 xta 22
  7 wtc 24
    atx
  7 xta 5
    a+x #1
  7 atx 25
  7 xta 15
  7 xts 25
 13 vjm b$eq
    uza .T199
  7 xta 15
  7 atx 27
    uj .T200
.T199:
  7 xta 15
    aox #0100000
  7 atx 27
.T200:
    xta
  7 a+x 5
  7 atx 29
  7 xta 27
  7 wtc 29
    atx
    utc allocb
    xta
  7 xts 1
 13 vjm b$gt
    uza .T208
  7 xta 1
    utc allocb
    atx
    uj .T209
.T208:
.T209:
    uj b$ret
    .text
    .globl realloc
realloc:
    its 13
 13 vjm b$save
 15 utm 47
  6 xta
    aax #00037777777777777
    xts
 13 vjm b$eq
    uza .T215
  6 xta 1
 14 vtm -1
 13 vjm malloc
    uj b$ret
.T215:
  6 xta
    aax #00037777777777777
  7 atx 3
  7 atx 4
    xta #037777777777777
  7 a+x 3
  7 atx 5
    xta
  7 a+x 5
  7 atx 6
  7 wtc 6
    xta
    aax #0100000
    uza .T227
  7 xta 3
    aox #0'64
 14 vtm -1
 13 vjm free
    uj .T228
.T227:
.T228:
    xta #037777777777777
  7 a+x 3
  7 atx 10
    xta
  7 a+x 10
  7 atx 11
  7 wtc 11
    xta
  7 a-x 3
  7 atx 13
  7 atx 14
  6 xta 1
 14 vtm -1
 13 vjm malloc
    aax #00037777777777777
  7 atx 16
  7 atx 17
  7 xta 16
    aox #0'64
    aax #00037777777777777
    xts
 13 vjm b$eq
    uza .T244
    xta
    uj b$ret
.T244:
  7 xta 16
  7 xts 3
 13 vjm b$eq
    uza .T248
  6 xta
    uj b$ret
.T248:
  7 xta 3
  7 atx 22
  7 xta 16
  7 atx 23
  6 xta 1
    xts #6
 13 vjm b$uadd
    xts #1
 13 vjm b$usub
    xts #6
 13 vjm b$udiv
  7 atx 26
  7 atx 27
  7 xta 26
  7 xts 13
 13 vjm b$ult
    uza .T257
  7 xta 26
  7 atx 14
    uj .T258
.T257:
.T258:
.L211:
  7 xta 14
  7 atx 29
  7 xta 14
    xts #1
 13 vjm b$usub
  7 atx 14
  7 xta 29
    xts
 13 vjm b$ne
    uza .L210
  7 xta 23
  7 atx 32
  7 xta 23
    a+x #1
  7 atx 23
  7 xta 22
  7 atx 34
  7 xta 22
    a+x #1
  7 atx 22
    xta
  7 a+x 34
  7 atx 36
  7 wtc 36
    xta
  7 atx 37
    xta
  7 a+x 32
  7 atx 38
  7 xta 37
  7 wtc 38
    atx
    uj .L211
.L210:
  7 xta 17
  7 xts 4
 13 vjm b$lt
    uza .T272
  7 xta 17
  7 a+x 27
  7 xts 4
 13 vjm b$ge
    xts
 13 vjm b$ne
  7 atx 42
    uj .T273
.T272:
    xta
  7 atx 42
.T273:
  7 xta 42
    uza .T278
  7 xta 17
  7 a+x 27
  7 a-x 4
  7 atx 44
  7 xta 17
  7 a+x 44
  7 atx 45
    xta
  7 a+x 45
  7 atx 46
    utc allocx
    xta
  7 wtc 46
    atx
    uj .T279
.T278:
.T279:
  7 xta 17
    aox #0'64
    uj b$ret
