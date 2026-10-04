#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
#include <gb/gb.h>
BANKREF_EXTERN(sound_bank)

// Plays the original PICO-8 sfx/music data on the GB APU.
void snd_init(void) BANKED;
void snd_update(void);           // call once per frame from VBL, with sound_bank mapped
void music(int8_t pattern) BANKED;  // -1 stops
void musiclvl(uint8_t lvl) BANKED;      // dynamic layering of game music (channels 1..3)
void sfx(uint8_t n) BANKED;
void sfx_stop(void) BANKED;

#endif
