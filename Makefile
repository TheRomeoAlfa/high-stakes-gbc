GBDK ?= $(HOME)/tools/gbdk
LCC = $(GBDK)/bin/lcc
PYTHON ?= python3

# MBC5 + RAM + battery, CGB only, 32KB SRAM bank count 1
LCCFLAGS = -Wl-m -Wl-j -Wl-yt0x1B -Wm-yC -Wl-ya1 -Wm-yn"HIGH STAKES" -autobank -Wm-yoA -Isrc
CFLAGS = -Wf--opt-code-size -Isrc
# hot paths (rendering, sound) are optimised for speed
FASTFLAGS = -Wf--opt-code-speed -Wf--max-allocs-per-node50000

SRC = src/main.c src/gfx.c src/sound.c src/game.c src/scenes.c src/gen/assets0.c src/gen/assetsb.c
OBJ = $(patsubst src/%.c,build/obj/%.o,$(SRC))
ROM = build/highstakes.gbc

all: $(ROM)

src/gen/assets.h src/gen/assets0.c src/gen/assetsb.c: tools/gen_assets.py tools/p8cart.py assets/highstakes.p8.png
	$(PYTHON) tools/gen_assets.py

build/obj/gfx.o build/obj/sound.o: CFLAGS += $(FASTFLAGS)

build/obj/%.o: src/%.c src/gen/assets.h src/*.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -c -o $@ $<

$(ROM): $(OBJ)
	$(LCC) $(LCCFLAGS) -o $@ $(OBJ)

clean:
	rm -rf build/obj $(ROM)

.PHONY: all clean
