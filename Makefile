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
VERSION := v0.2

SRC_C := src/main.c src/sound.c src/engine.c src/editor.c src/save.c src/sync.c src/pool.c $(BUILD)/notes.c
SRC_S := src/lowcode.s src/irq.s src/eeprom.s src/cart.s

# cc65 2.18 gotcha: lynx/defdir.s references __LOWCODE_SIZE__, but marks the
# LOWCODE segment optional, so a build with no LOWCODE data fails to link.
# src/lowcode.s exists only to instantiate the segment.

all: $(ROM)

# build stamp: short git hash, trailing + when the tree is dirty; only
# rewritten when it changes so it doesn't force rebuilds (sibling pattern)
BUILDID := $(shell git rev-parse --short HEAD 2>/dev/null)$(shell git diff-index --quiet HEAD -- 2>/dev/null || echo +)
$(shell mkdir -p $(BUILD))
$(shell printf '#define BUILDID "%s"\n#define VERSION "%s"\n' '$(BUILDID)' '$(VERSION)' > $(BUILD)/buildid.h.tmp; \
        cmp -s $(BUILD)/buildid.h.tmp $(BUILD)/buildid.h 2>/dev/null || cp $(BUILD)/buildid.h.tmp $(BUILD)/buildid.h; \
        rm -f $(BUILD)/buildid.h.tmp)

$(BUILD)/pool.bin: tools/alynxdj_pool.py
	python3 tools/alynxdj_pool.py samples $@

$(BUILD)/notes.h $(BUILD)/notes.c &: tools/maketables.py
	python3 tools/maketables.py $(BUILD)/notes.h

$(BUILD)/font.h: tools/makefont.py
	python3 tools/makefont.py $@

$(BUILD)/logo.h: tools/makelogo.py art/aldj.png
	python3 tools/makelogo.py

$(ROM): $(SRC_C) $(SRC_S) src/tracker.h $(CFG) $(BUILD)/font.h $(BUILD)/logo.h $(BUILD)/notes.h $(BUILD)/buildid.h $(BUILD)/pool.bin
	cl65 -t lynx -O -C $(CFG) -m $(BUILD)/alynxdj.map -o $@ $(SRC_C) $(SRC_S)
	python3 -c "p='$@'; d=bytearray(open(p,'rb').read()); d[60]=5; open(p,'wb').write(d)"  # .lnx byte 60: 93C86 EEPROM (2KB)
	python3 -c "\
p='$@'; d=bytearray(open(p,'rb').read()); \
assert len(d) <= 64+40*1024, 'program spills into the pool block'; \
d += b'\0'*(64+40*1024-len(d)); \
d += open('$(BUILD)/pool.bin','rb').read(); \
d += b'\0'*(64+256*1024-len(d)); \
open(p,'wb').write(d)"  # pool at cart block 40, pad to 256KB

# --- headless screenshot + audio capture (no GUI / permissions needed) ---
EMUDIR    := tools/emu
EMUCORE   := $(EMUDIR)/handy_libretro.dylib
RETROSHOT := $(EMUDIR)/retroshot
FRAMES    ?= 120
BTN       ?=

$(RETROSHOT): $(EMUDIR)/harness.c $(EMUDIR)/libretro.h
	clang -O2 -o $@ $(EMUDIR)/harness.c -I$(EMUDIR)

$(EMUDIR)/duoshot: $(EMUDIR)/duoshot.c $(EMUDIR)/libretro.h
	clang -O2 -o $@ $(EMUDIR)/duoshot.c -I$(EMUDIR)

shot: $(ROM) $(RETROSHOT)
	$(RETROSHOT) $(EMUCORE) $(ROM) $(BUILD)/shot.ppm $(FRAMES) $(BTN)
	python3 -c "from PIL import Image; Image.open('$(BUILD)/shot.ppm').save('$(BUILD)/shot.png')"
	@echo "wrote $(BUILD)/shot.png"

# version-only release copy (attach these to GitHub releases)
dist: $(ROM)
	cp $(ROM) $(BUILD)/alynxdj_$(subst .,_,$(VERSION)).lnx
	@echo "wrote $(BUILD)/alynxdj_$(subst .,_,$(VERSION)).lnx"

clean:
	rm -rf $(BUILD)

.PHONY: all shot dist clean
