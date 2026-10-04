#ifndef COMMON_H
#define COMMON_H
#include <gb/gb.h>
#include <stdint.h>
#include "gen/assets.h"
#include "gfx.h"
#include "sound.h"
#include "game.h"

// scenes
enum { SC_CREDITS, SC_START, SC_INTRO, SC_MENU, SC_GAME, SC_ASK, SC_WINING, SC_END };
extern uint8_t scene;

// persistent
extern int16_t blood, tries, chain, bchain;
extern uint8_t wbin;
extern uint8_t unlocked[4];
extern int16_t high[4];

// match state
extern int16_t blood_txt, wining, wining_txt, stakes, stakes_txt, mult;
extern uint8_t ticksfx, losses_desc;
extern uint8_t vampval, dropboxes, droptoken, scoremode, mnucur;
extern uint8_t round_, bonusmode;
extern char wins[8];
extern uint8_t gtxt_changed;   // 1 stakes, 2 wining, 4 blood

extern const char *const vname[4];
extern const char *const vquote[4];
extern const uint8_t vmult[4];
extern const int16_t vcost[4];
extern const uint8_t vtoken[4];

// shared helpers (game.c)
void update_gtxt(void) BANKED;
void savegame(void) BANKED;
void tempsave(void) BANKED;
void newgame(void) BANKED;
void startmatch(void) BANKED;
void dealround(void) BANKED;
void scene_game(void) BANKED;

// scenes (scenes.c)
void scene_credits(void) BANKED;
void scene_start(void) BANKED;
void scene_intro(void) BANKED;
void scene_menu(void) BANKED;
void scene_ask(void) BANKED;
void scene_wining(void) BANKED;
void scene_end(void) BANKED;
void gotowin(void) BANKED;
void goverprint_set(const char *g1, const char *g2) BANKED;
void bonusround(void) BANKED;

// card helpers
void draw_card(uint8_t tx, uint8_t ty, uint8_t frame) BANKED;
#define CARD_NONE 255

// dialog box shared by intro / ask
void dlg_setup(uint8_t ty, uint8_t with_card) BANKED;
void dlg_say(const char *s) BANKED;
uint8_t dlg_tick(void) BANKED;   // typewriter; returns 1 when complete
void dlg_arrow(uint8_t on) BANKED;

void card_pals_load(void) BANKED;
void reset_screen(void) BANKED;

#endif
