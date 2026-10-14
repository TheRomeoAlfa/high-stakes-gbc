// Dealing a round (dealround): shuffle, hints, slot tokens, and the cards' flight in.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"
#include "board.h"

void dealround(void) BANKED {
    uint8_t vals[9], nv = 9, i, k, h, nt;
    uint8_t targs[9];
    bonusmode = 0;
    stabdx = 0;
    stabdy = 0;
    stabonus = 7;
    pflockx = 0;
    pflocky = pflockdest = -28;
    pflockard = 255;
    curchip = 0;
    curstab = 0;
    showcur = 0;
    dangrect = -1;
    helpi = 0;
    if (staby != 0) { stabx = -20 * 16; staby = 0; }
    musiclvl(0);
    for (i = 0; i < 9; i++) vals[i] = i + 2;
    for (i = 0; i < 9; i++) {
        k = rnd(nv);
        memset(&cards[i], 0, sizeof(card_t));
        cards[i].val = vals[k];
        vals[k] = vals[--nv];
        cards[i].hidden = 1;
        cards[i].delay = 10 * (i + 1);
        cards[i].fr = 254;
        cards[i].ani = 2;
        fx[i] = 54 << 4;    // the original's (54, -70) card centre (12.4)
        fy[i] = -93 << 4;
        fz[i] = 10 << 8;
        fzd[i] = 0;
        if (cards[i].val == 10) vcard = i;
    }
    do autoflip = rnd(9); while (cards[autoflip].val == 10);
    cards[autoflip].hidden = 0;
    cur = autoflip + 1;
    for (h = 9; h >= 2; h--) {
        if (vampval == 13 && h == cards[autoflip].val) continue;  // asshole mode
        nt = 0;
        for (i = 0; i < 9; i++)
            if (cards[i].hidden && cards[i].val >= h && cards[i].hint == 0 && !(vampval == 13 && i == vcard)) targs[nt++] = i;
        if (nt) cards[targs[rnd(nt)]].hint = h;
    }
    if (vampval == 13) cards[vcard].hint = cards[autoflip].val;
    cards[autoflip].hidden = 1;
    for (i = 0; i < 6; i++) gs[i] = rnd(2) ? 4 : 2;
    if (dropboxes) {
        if (vampval == 13) {
            uint8_t va = vcard % 3, vb = 3 + vcard / 3;
            if (rnd(5) < 3) gs[rnd(2) ? va : vb] = 10;
            else {
                do k = rnd(6); while (k == va || k == vb);
                gs[k] = 10;
            }
        } else gs[rnd(6)] = 10;
    }
    memset(chips, 0, sizeof(chips));
    chip_hi = 0;
    result = 0;
    stakes = 0;
    round_++;
    bonus_dirty = 1;
}

void bonusround(void) BANKED {
    dealround();
    bonusmode = 1;
    stakes = wining;
    stabdx = -30 * 16;
}


// stake motion, eased with shifts instead of 16-bit divisions (~1/10 and 1/4 per frame)
void stake_ease(void) BANKED {
    if (stabx != stabdx) {
        int16_t d = stabdx - stabx;
        int16_t s = (d >> 3) - (d >> 5);
        stabx += s ? s : (d > 0 ? 1 : -1);
    }
    if (staby != stabdy) {
        int16_t d = stabdy - staby;
        int16_t s = d >> 2;
        staby += s ? s : (d > 0 ? 1 : -1);
    }
}
