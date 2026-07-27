// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Terminal driving and cursor-motion optimisation.
//
// Nothing here compares two pointers; it is all column and line arithmetic, which is why
// this is the largest file in the library and one of the few that needed no rewriting for
// the machine.  The two commented-out backtab blocks are 4.3BSD's own -- left as they were
// found, because deleting them would lose the record that BT was tried and abandoned.
//
#include "internal.h"
#include <string.h>

#define HARDTABS 8

static int outcol, outline, destcol, destline;

// Counting mode: plod() is asked to price a motion before performing it, and in that mode
// plodput() decrements a budget instead of emitting anything.
static int plodcnt, plodflg;

static int plodput(int c)
{
    if (plodflg) {
        plodcnt--;
        return 0;
    }
    return _putchar(c);
}

// The column that results from being in column col and hitting a tab, where tabs are set
// every ts columns.  Works for col > COLS even when ts does not divide COLS.
static int tabcol(int col, int ts)
{
    int offset;

    if (col >= COLS) {
        offset = COLS * (col / COLS);
        col -= offset;
    } else
        offset = 0;
    return col + ts - (col % ts) + offset;
}

// Move to (destline, destcol) using local motions.  With cnt non-zero nothing is emitted
// and the return value is what is left of the budget -- negative if the motion would cost
// more than cnt characters.
static int plod(int cnt)
{
    register int i, j, k;
    register int soutcol, soutline;

    plodcnt = plodflg = cnt;
    soutcol           = outcol;
    soutline          = outline;
    // Consider homing and moving down/right from there, versus moving directly with local
    // motions to the right spot.
    if (HO) {
        // i is the cost to home and tab/space right to the proper column.  This assumes ND
        // costs one character, so i + destcol is the cost of the motion with home.
        if (GT)
            i = (destcol / HARDTABS) + (destcol % HARDTABS);
        else
            i = destcol;
        // j is the cost to move locally without homing.
        if (destcol >= outcol) { // motion is to the right
            j = destcol / HARDTABS - outcol / HARDTABS;
            if (GT && j)
                j += destcol % HARDTABS;
            else
                j = destcol - outcol;
        } else
            // Leftward motion only works if we can backspace.
            if (outcol - destcol <= i && (BS || BC))
                i = j = outcol - destcol; // cheaper to backspace
            else
                j = i + 1; // impossibly expensive

        // k is the absolute vertical distance.
        k = outline - destline;
        if (k < 0)
            k = -k;
        j += k;

        // Decision.  We may not have a choice, if there is no UP.
        if (i + destline < j || (!UP && destline < outline)) {
            // Cheaper to home.  Do it now and pretend it was a regular local motion.
            tputs(HO, 0, plodput);
            outcol = outline = 0;
        } else if (LL) {
            // Quickly consider homing down and moving from there.  Assume LL costs two.
            k = (LINES - 1) - destline;
            if (i + k + 2 < j && (k <= 0 || UP)) {
                tputs(LL, 0, plodput);
                outcol  = 0;
                outline = LINES - 1;
            }
        }
    } else
        // No home and no up means it is impossible.
        if (!UP && destline < outline)
            return -1;
    if (GT)
        i = destcol % HARDTABS + destcol / HARDTABS;
    else
        i = destcol;
    /*
        if (BT && outcol > destcol && (j = (((outcol+7) & ~7) - destcol - 1) >> 3)) {
            j *= (k = strlen(BT));
            if ((k += (destcol&7)) > 4)
                j += 8 - (destcol&7);
            else
                j += k;
        }
        else
    */
    j = outcol - destcol;
    // If we will later need a \n which will turn into a \r\n by the system or the terminal,
    // then do not bother trying to \r.
    if (!_pfast && outline < destline)
        goto dontcr;
    // If the terminal will do a \r\n and there is not room for it, we cannot afford a \r.
    if (NC && outline >= destline)
        goto dontcr;
    // If it will be cheaper, or if we cannot back up, send a return first.
    if (j > i + 1 || (outcol > destcol && !BS && !BC)) {
        // BUG: this does not take the (possibly long) length of CR into account.
        if (CR)
            tputs(CR, 0, plodput);
        else
            plodput('\r');
        if (NC) {
            if (NL)
                tputs(NL, 0, plodput);
            else
                plodput('\n');
            outline++;
        }
        outcol = 0;
    }
dontcr:
    while (outline < destline) {
        outline++;
        if (NL)
            tputs(NL, 0, plodput);
        else {
            plodput('\n');
            if (!_pfast)
                outcol = 0;
        }
        if (plodcnt < 0)
            goto out;
    }
    if (BT)
        k = strlen(BT);
    while (outcol > destcol) {
        if (plodcnt < 0)
            goto out;
        /*
                if (BT && outcol - destcol > k + 4) {
                    tputs(BT, 0, plodput);
                    outcol--;
                    outcol &= ~7;
                    continue;
                }
        */
        outcol--;
        if (BC)
            tputs(BC, 0, plodput);
        else
            plodput('\b');
    }
    while (outline > destline) {
        outline--;
        tputs(UP, 0, plodput);
        if (plodcnt < 0)
            goto out;
    }
    if (GT && destcol - outcol > 1) {
        for (;;) {
            i = tabcol(outcol, HARDTABS);
            if (i > destcol)
                break;
            if (TA)
                tputs(TA, 0, plodput);
            else
                plodput('\t');
            outcol = i;
        }
        if (destcol - outcol > 4 && i < COLS && (BC || BS)) {
            if (TA)
                tputs(TA, 0, plodput);
            else
                plodput('\t');
            outcol = i;
            while (outcol > destcol) {
                outcol--;
                if (BC)
                    tputs(BC, 0, plodput);
                else
                    plodput('\b');
            }
        }
    }
    while (outcol < destcol) {
        // Move one character to the right.  We do not use ND space, because it is better to
        // just print the character we are moving over -- provided the screen image agrees
        // with the terminal about what that character is, standout included.
        //
        // Braced rather than v7's dangling if/else with a `goto nondes' INTO the else arm.
        // Same four cases, in the same order; the label is a `moved:' at the bottom now,
        // which is a jump out of a block rather than into one.
        if (_win != NULL && !plodflg) {
            i = curscr->_y[outline][outcol];
            if ((i & _STANDOUT) == (curscr->_flags & _STANDOUT)) {
                _putchar(i);
                goto moved;
            }
        } else if (_win != NULL) { // counting: avoid a complex calculation
            plodcnt--;
            goto moved;
        }
        if (ND)
            tputs(ND, 0, plodput);
        else
            plodput(' ');
    moved:
        outcol++;
        if (plodcnt < 0)
            goto out;
    }
out:
    if (plodflg) {
        outcol  = soutcol;
        outline = soutline;
    }
    return plodcnt;
}

// Get to (destline, destcol), scrolling and wrapping as required, then choose between
// direct cursor addressing and local motions -- whichever is fewer characters.
static void fgoto(void)
{
    char *cgp;
    int l, c;

    if (destcol >= COLS) {
        destline += destcol / COLS;
        destcol %= COLS;
    }
    if (outcol >= COLS) {
        l = (outcol + 1) / COLS;
        outline += l;
        outcol %= COLS;
        if (AM == 0) {
            while (l > 0) {
                if (_pfast) {
                    if (CR)
                        _puts(CR);
                    else
                        _putchar('\r');
                }
                if (NL)
                    _puts(NL);
                else
                    _putchar('\n');
                l--;
            }
            outcol = 0;
        }
        if (outline > LINES - 1) {
            destline -= outline - (LINES - 1);
            outline = LINES - 1;
        }
    }
    if (destline >= LINES) {
        l        = destline;
        destline = LINES - 1;
        if (outline < LINES - 1) {
            c = destcol;
            if (_pfast == 0 && !CA)
                destcol = 0;
            fgoto();
            destcol = c;
        }
        while (l >= LINES) {
            // The following linefeed (or simulation of one) is supposed to scroll up the
            // screen, since we are on the bottom line.  We assume linefeed will scroll; if
            // `ns' is in the capability list this will not work.  There should probably be
            // an `sc' capability, but `sf' will generally take its place if it works.
            if (NL && _pfast)
                _puts(NL);
            else
                _putchar('\n');
            l--;
            if (_pfast == 0)
                outcol = 0;
        }
    }
    if (destline < outline && !(CA || UP))
        destline = outline;
    if (CA) {
        cgp = tgoto(CM, destcol, destline);
        if (plod(strlen(cgp)) > 0)
            plod(0);
        else
            tputs(cgp, 0, _putchar);
    } else
        plod(0);
    outline = destline;
    outcol  = destcol;
}

void mvcur(int ly, int lx, int y, int x)
{
    destcol  = x;
    destline = y;
    outcol   = lx;
    outline  = ly;
    fgoto();
}
