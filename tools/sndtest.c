// Sound test ROM: plays one music pattern (or an sfx) for comparing the GB
// sound engine against tools/p8synth.py. Build with tools/sndtest.sh.
#include <gb/gb.h>
#include <gb/cgb.h>
#include "sound.h"

#ifndef PAT
#define PAT 0
#endif
#ifndef LVL
#define LVL 3
#endif

void main(void) {
    uint8_t save;
    cpu_fast();
    DISPLAY_ON;
    snd_init();
#ifdef SFX
    sfx(SFX);
#else
    musiclvl(LVL);
    music(PAT);
#endif
    while (1) {
        vsync();
        save = _current_bank;
        SWITCH_ROM(BANK(sound_bank));
        snd_update();
        SWITCH_ROM(save);
    }
}
