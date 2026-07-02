# ALYNXDJ — LSDJ-inspired music tracker for the Atari Lynx
#
#   make        build/alynxdj.lnx
#   make shot   headless screenshot -> build/shot.png (+ build/shot.ppm.wav audio)
#   make clean
#
# Toolchain: cc65 (cl65 -t lynx). The Handy libretro core runs the ROM with no
# BIOS (HLE boot; it logs a lynxboot.img warning, which is harmless).

BUILD := build
ROM   := $(BUILD)/alynxdj.lnx

SRC_C := src/main.c
SRC_S := src/lowcode.s

# cc65 2.18 gotcha: lynx/defdir.s references __LOWCODE_SIZE__, but the stock
# lynx.cfg marks LOWCODE optional, so a build with no LOWCODE data fails to
# link. src/lowcode.s exists only to instantiate the segment.

all: $(ROM)

$(ROM): $(SRC_C) $(SRC_S) | $(BUILD)
	cl65 -t lynx -O -o $@ $(SRC_C) $(SRC_S)

$(BUILD):
	mkdir -p $(BUILD)

# --- headless screenshot + audio capture (no GUI / permissions needed) ---
EMUDIR    := tools/emu
EMUCORE   := $(EMUDIR)/handy_libretro.dylib
RETROSHOT := $(EMUDIR)/retroshot
FRAMES    ?= 120
BTN       ?=

$(RETROSHOT): $(EMUDIR)/harness.c $(EMUDIR)/libretro.h
	clang -O2 -o $@ $(EMUDIR)/harness.c -I$(EMUDIR)

shot: $(ROM) $(RETROSHOT)
	$(RETROSHOT) $(EMUCORE) $(ROM) $(BUILD)/shot.ppm $(FRAMES) $(BTN)
	python3 -c "from PIL import Image; Image.open('$(BUILD)/shot.ppm').save('$(BUILD)/shot.png')"
	@echo "wrote $(BUILD)/shot.png"

clean:
	rm -rf $(BUILD)

.PHONY: all shot clean
