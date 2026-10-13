// Save data in cart SRAM: progress, high scores, unlocked opponents.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "common.h"

// ------------------------------------------------------------------ save (cart SRAM)
typedef struct {
    uint8_t magic[4];
    int16_t blood, tries, chain, bchain;
    uint8_t unl[4];
    int16_t high[4];
    uint8_t wbin;
} save_t;
static save_t *const SAVEP = (save_t *)0xA000;
#define SAVE SAVEP

void savegame(void) BANKED {
    uint8_t i;
    ENABLE_RAM;
    SWITCH_RAM(0);
    SAVE->magic[0] = 'H'; SAVE->magic[1] = 'S'; SAVE->magic[2] = 'G'; SAVE->magic[3] = '1';
    SAVE->blood = blood;
    SAVE->tries = tries;
    SAVE->chain = chain;
    SAVE->bchain = bchain;
    SAVE->wbin = wbin;
    for (i = 0; i < 4; i++) {
        SAVE->unl[i] = unlocked[i];
        SAVE->high[i] = high[i];
    }
    DISABLE_RAM;
}

void loadgame(void) BANKED {
    uint8_t i;
    ENABLE_RAM;
    SWITCH_RAM(0);
    if (SAVE->magic[0] == 'H' && SAVE->magic[1] == 'S' && SAVE->magic[2] == 'G' && SAVE->magic[3] == '1') {
        blood = SAVE->blood;
        tries = SAVE->tries;
        chain = SAVE->chain;
        bchain = SAVE->bchain;
        wbin = SAVE->wbin;
        for (i = 0; i < 4; i++) {
            unlocked[i] = SAVE->unl[i];
            high[i] = SAVE->high[i];
        }
        unlocked[0] = 1;
        DISABLE_RAM;
    } else {
        DISABLE_RAM;
        blood = tries = chain = bchain = 0;
        wbin = 0;
        for (i = 0; i < 4; i++) { unlocked[i] = i == 0; high[i] = 0; }
        savegame();
    }
}

void tempsave(void) BANKED {
    int16_t tb;
    if (scoremode) return;
    tb = blood;
    blood += wining;
    if (blood < 0) blood = 0;
    if (blood > 5000) blood = 5000;
    savegame();
    blood = tb;
}

void newgame(void) BANKED {
    tries++;
    blood = 20;
    unlocked[0] = 1;
    unlocked[1] = unlocked[2] = unlocked[3] = 0;
    savegame();
}


// ------------------------------------------------------------------ help box texts (game.c)
// kept here, out of game.c's full bank; copied out for printing
static const char *const helptxt[6] = {
    "place on a face-down card.\ncard value is equal or\nhigher than token number",
    "place on a face-up card.\ncompares the values of\nadjecent face-down cards",
    "stab a card if you think\nit is a vampire. bonus ML\nif you get it right.",
    "flip all cards in this\ncolumn face-up to unlock\nthis hint token.",
    "flip all cards in this line\nface-up to unlock this\nhint token.",
    "highlights a 2x2 area where\nthe vampire card will be."};

void help_text(uint8_t i, char *out) BANKED {
    strcpy(out, helptxt[i]);
}
