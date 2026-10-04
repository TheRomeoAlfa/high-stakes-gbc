#ifndef ASSETS_H
#define ASSETS_H
#include <gbdk/platform.h>
#include <stdint.h>

extern const uint8_t font_rows[640];
extern const uint8_t font_w[128];
extern const uint8_t big_w[36];
extern const uint8_t big_kern[36];
extern const uint16_t big_rows[360];
#define N_CARD_TILES 217
#define CF_BACK 0
#define CF_EDGE 3
#define CF_FACE(v) (4 + ((v) - 2) * 3)
extern const uint8_t card_tiles[3472];
BANKREF_EXTERN(card_tiles)
extern const uint8_t card_fmap[480];
extern const uint8_t card_fattr[480];
extern const uint16_t card_pals[16];
extern const uint8_t slot_map[42];
extern const uint8_t slot_attr[42];
extern const uint8_t sep_map[20];
extern const uint8_t sep_attr[20];
#define N_GAME_TILES 36
extern const uint8_t game_tiles[576];
BANKREF_EXTERN(game_tiles)
extern const uint16_t obj_pals[32];
extern const uint8_t spr_tiles[1632];
BANKREF_EXTERN(spr_tiles)
#define N_SPR_TILES 102
#define SPR_HAND 0
#define SPRPAL_HAND 0
#define SPRW_HAND 16
#define SPRN_HAND 2
#define SPR_TOK2 4
#define SPRPAL_TOK2 1
#define SPRW_TOK2 16
#define SPRN_TOK2 2
#define SPR_TOK3 8
#define SPRPAL_TOK3 1
#define SPRW_TOK3 16
#define SPRN_TOK3 2
#define SPR_TOK4 12
#define SPRPAL_TOK4 1
#define SPRW_TOK4 16
#define SPRN_TOK4 2
#define SPR_TOK5 16
#define SPRPAL_TOK5 1
#define SPRW_TOK5 16
#define SPRN_TOK5 2
#define SPR_TOK6 20
#define SPRPAL_TOK6 1
#define SPRW_TOK6 16
#define SPRN_TOK6 2
#define SPR_TOK7 24
#define SPRPAL_TOK7 1
#define SPRW_TOK7 16
#define SPRN_TOK7 2
#define SPR_TOK8 28
#define SPRPAL_TOK8 1
#define SPRW_TOK8 16
#define SPRN_TOK8 2
#define SPR_TOK9 32
#define SPRPAL_TOK9 1
#define SPRW_TOK9 16
#define SPRN_TOK9 2
#define SPR_CHIP_PLUS 36
#define SPRPAL_CHIP_PLUS 1
#define SPRW_CHIP_PLUS 16
#define SPRN_CHIP_PLUS 2
#define SPR_CHIP_ARROWS 40
#define SPRPAL_CHIP_ARROWS 2
#define SPRW_CHIP_ARROWS 16
#define SPRN_CHIP_ARROWS 2
#define SPR_CHIP_BOX 44
#define SPRPAL_CHIP_BOX 3
#define SPRW_CHIP_BOX 16
#define SPRN_CHIP_BOX 2
#define SPR_ARR_L 48
#define SPRPAL_ARR_L 2
#define SPRW_ARR_L 8
#define SPR_ARR_R 50
#define SPRPAL_ARR_R 2
#define SPRW_ARR_R 8
#define SPR_ARR_U 52
#define SPRPAL_ARR_U 2
#define SPRW_ARR_U 8
#define SPR_ARR_D 54
#define SPRPAL_ARR_D 2
#define SPRW_ARR_D 8
#define SPR_STAKE 56
#define SPRPAL_STAKE 4
#define SPRW_STAKE 8
#define SPRN_STAKE 4
#define SPR_STAKE_HL 64
#define SPRPAL_STAKE_HL 6
#define SPRW_STAKE_HL 16
#define SPRN_STAKE_HL 8
#define SPR_PFLOCK 80
#define SPRPAL_PFLOCK 5
#define SPRW_PFLOCK 8
#define SPRN_PFLOCK 2
#define SPR_PFLOCKCUT 84
#define SPRPAL_PFLOCKCUT 5
#define SPRW_PFLOCKCUT 8
#define SPRN_PFLOCKCUT 2
#define SPR_STABCUR 88
#define SPRPAL_STABCUR 5
#define SPRW_STABCUR 16
#define SPRN_STABCUR 2
#define SPR_CORNER 92
#define SPRPAL_CORNER 7
#define SPRW_CORNER 8
#define SPR_DRIP 94
#define SPRPAL_DRIP 7
#define SPRW_DRIP 8
#define SPR_DROP 96
#define SPRPAL_DROP 7
#define SPRW_DROP 8
#define SPR_MARR_L 98
#define SPRPAL_MARR_L 7
#define SPRW_MARR_L 8
#define SPR_MARR_R 100
#define SPRPAL_MARR_R 7
#define SPRW_MARR_R 8
#define IMG_BASE 217
extern const uint8_t whisky_tiles[320];
BANKREF_EXTERN(whisky_tiles)
extern const uint8_t whisky_map[20];
extern const uint8_t whisky_attr[20];
extern const uint16_t whisky_pals[12];
#define N_WHISKY_TILES 20
#define WHISKY_W 4
#define WHISKY_H 5
extern const uint8_t glasses_tiles[608];
BANKREF_EXTERN(glasses_tiles)
extern const uint8_t glasses_map[55];
extern const uint8_t glasses_attr[55];
extern const uint16_t glasses_pals[28];
#define N_GLASSES_TILES 38
#define GLASSES_W 11
#define GLASSES_H 5
extern const uint8_t sunset_tiles[704];
BANKREF_EXTERN(sunset_tiles)
extern const uint8_t sunset_map[44];
extern const uint8_t sunset_attr[44];
extern const uint16_t sunset_pals[28];
#define N_SUNSET_TILES 44
#define SUNSET_W 11
#define SUNSET_H 4
extern const uint8_t logo_tiles[416];
BANKREF_EXTERN(logo_tiles)
extern const uint8_t logo_parts[52];
#define N_LOGO_PARTS 13
#define N_LOGO_TILES 26
extern const uint16_t logo_pals[12];
extern const uint8_t ring_tiles[1040];
BANKREF_EXTERN(ring_tiles)
extern const uint8_t ring_map[260];
extern const uint8_t ring_attr[260];
extern const uint8_t ring_thick[260];
#define N_RING_TILES 65
extern const uint8_t vial_px[171];
#endif
