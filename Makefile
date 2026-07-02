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
CFG   := alynxdj.cfg

SRC_C := src/main.c src/sound.c src/engine.c
SRC_S := src/lowcode.s src/irq.s

# cc65 2.18 gotcha: lynx/defdir.s references __LOWCODE_SIZE__, but marks the
# LOWCODE segment optional, so a build with no LOWCODE data fails to link.
# src/lowcode.s exists only to instantiate the segment.

all: $(ROM)

# build stamp: short git hash, trailing + when the tree is dirty; only
# rewritten when it changes so it doesn't force rebuilds (sibling pattern)
BUILDID := $(shell git rev-parse --short HEAD 2>/dev/null)$(shell git diff-index --quiet HEAD -- 2>/dev/null || echo +)
$(shell mkdir -p $(BUILD))
$(shell [ "`cat $(BUILD)/buildid.h 2>/dev/null`" = '#define BUILDID "$(BUILDID)"' ] || \
        echo '#define BUILDID "$(BUILDID)"' > $(BUILD)/buildid.h)

$(BUILD)/notes.h: tools/maketables.py
	python3 tools/maketables.py $@

$(BUILD)/font.h: tools/makefont.py
	python3 tools/makefont.py $@

$(ROM): $(SRC_C) $(SRC_S) $(CFG) $(BUILD)/font.h $(BUILD)/notes.h $(BUILD)/buildid.h
	cl65 -t lynx -O -C $(CFG) -o $@ $(SRC_C) $(SRC_S)

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
