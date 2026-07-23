# ALYNXDJ — LSDJ-inspired music tracker for the Atari Lynx
#
#   make        build/alynxdj.lnx
#   make sample-timing-test  build/alynxdj-no-meters-no-peaks.lnx
#   make factory-samples  rebuild the portable factory sample bank
#   make SAMPLE_BANK=/path/custom.bin  inject a portable custom sample bank
#   make shot   headless screenshot -> build/shot.png (+ build/shot.ppm.wav audio)
#   make clean
#
# Toolchain: cc65 (cl65 -t lynx). The Handy libretro core runs the ROM with no
# BIOS (HLE boot; it logs a lynxboot.img warning, which is harmless).

BUILD := build
.DEFAULT_GOAL := all
ROM   := $(BUILD)/alynxdj.lnx
ROM_NOMETERS := $(BUILD)/alynxdj-no-meters.lnx
ROM_NOPEAKS := $(BUILD)/alynxdj-no-meters-no-peaks.lnx
CFG   := alynxdj.cfg
VERSION := v0.51
PYTHON ?= python3
FACTORY_SAMPLE_BANK := samples/alynxdj-factory-samples.bin
SAMPLE_BANK ?= $(FACTORY_SAMPLE_BANK)
SAMPLE_POOL_CAPACITY := 214016
ifneq ($(SAMPLE_BANK),$(FACTORY_SAMPLE_BANK))
SAMPLE_BANK_FORCE := FORCE_SAMPLE_BANK
.PHONY: FORCE_SAMPLE_BANK
FORCE_SAMPLE_BANK:
endif

SRC_C := src/main.c src/sound.c src/engine.c src/editor.c src/save.c src/sync.c src/pool.c $(BUILD)/notes.c
SRC_S := src/lowcode.s src/irq.s src/eeprom.s src/cart.s src/editor_alloc.s

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

.PHONY: factory-samples
factory-samples:
	$(PYTHON) tools/alynxdj_pool.py samples $(FACTORY_SAMPLE_BANK)

$(BUILD)/notes.h $(BUILD)/notes.c &: tools/maketables.py
	$(PYTHON) tools/maketables.py $(BUILD)/notes.h

$(BUILD)/font.h: tools/makefont.py
	$(PYTHON) tools/makefont.py $@

$(BUILD)/logo.h: tools/makelogo.py art/aldj.png
	$(PYTHON) tools/makelogo.py

$(ROM): $(SRC_C) $(SRC_S) src/tracker.h $(CFG) $(BUILD)/font.h $(BUILD)/logo.h $(BUILD)/notes.h $(BUILD)/buildid.h $(SAMPLE_BANK) $(SAMPLE_BANK_FORCE)
	cl65 -t lynx -O -C $(CFG) -m $(BUILD)/alynxdj.map -o $@ $(SRC_C) $(SRC_S)
	$(PYTHON) -c "p='$@'; d=bytearray(open(p,'rb').read()); d[60]=5; open(p,'wb').write(d)"  # .lnx byte 60: 93C86 EEPROM (2KB)
	$(PYTHON) tools/alynxdj_pool.py --validate $(SAMPLE_BANK) --capacity $(SAMPLE_POOL_CAPACITY)
	$(PYTHON) -c "\
p='$@'; d=bytearray(open(p,'rb').read()); \
aux1=open('$(BUILD)/aux1.bin','rb').read(); \
aux2=open('$(BUILD)/aux2.bin','rb').read(); \
aux3=open('$(BUILD)/aux3.bin','rb').read(); \
mid=open('$(BUILD)/auxmidi.bin','rb').read(); \
assert len(d) <= 64+40*1024, 'program spills into the RAM overlay blocks'; \
assert len(mid) <= 0x600, 'live helper overlay overflow'; \
assert len(aux1) <= 0x700 and len(aux2) <= 0x600 and len(aux3) <= 0x2E0, 'RAM overlay overflow'; \
d += b'\0'*(64+40*1024-len(d)); d += aux1; \
d += b'\0'*(64+42*1024-len(d)); d += aux2; \
d += b'\0'*(64+44*1024-len(d)); d += aux3; \
d += b'\0'*(64+45*1024-len(d)); \
d += open('$(SAMPLE_BANK)','rb').read(); \
d += b'\0'*(64+254*1024-len(d)); d += mid; \
d += b'\0'*(64+256*1024-len(d)); \
open(p,'wb').write(d)"  # overlays 40/42/44/254, sample bank at 45, 256KB cart

# Hardware timing diagnostic: identical engine/audio path, but the main loop
# never redraws the four 16-cell channel meters. "NM" appears in the top bar
# and the filename is distinct so this cannot be mistaken for the normal ROM.
$(ROM_NOMETERS): $(SRC_C) $(SRC_S) src/tracker.h $(CFG) $(BUILD)/font.h $(BUILD)/logo.h $(BUILD)/notes.h $(BUILD)/buildid.h $(SAMPLE_BANK) $(SAMPLE_BANK_FORCE)
	cl65 -t lynx -O -D ALYNXDJ_NO_METERS=1 -C $(CFG) -m $(BUILD)/alynxdj-no-meters.map -o $@ $(SRC_C) $(SRC_S)
	$(PYTHON) -c "p='$@'; d=bytearray(open(p,'rb').read()); d[60]=5; open(p,'wb').write(d)"
	$(PYTHON) tools/alynxdj_pool.py --validate $(SAMPLE_BANK) --capacity $(SAMPLE_POOL_CAPACITY)
	$(PYTHON) -c "\
p='$@'; d=bytearray(open(p,'rb').read()); \
aux1=open('$(BUILD)/aux1.bin','rb').read(); \
aux2=open('$(BUILD)/aux2.bin','rb').read(); \
aux3=open('$(BUILD)/aux3.bin','rb').read(); \
mid=open('$(BUILD)/auxmidi.bin','rb').read(); \
assert len(d) <= 64+40*1024, 'program spills into the RAM overlay blocks'; \
assert len(mid) <= 0x600, 'live helper overlay overflow'; \
assert len(aux1) <= 0x700 and len(aux2) <= 0x600 and len(aux3) <= 0x2E0, 'RAM overlay overflow'; \
d += b'\0'*(64+40*1024-len(d)); d += aux1; \
d += b'\0'*(64+42*1024-len(d)); d += aux2; \
d += b'\0'*(64+44*1024-len(d)); d += aux3; \
d += b'\0'*(64+45*1024-len(d)); \
d += open('$(SAMPLE_BANK)','rb').read(); \
d += b'\0'*(64+254*1024-len(d)); d += mid; \
d += b'\0'*(64+256*1024-len(d)); \
open(p,'wb').write(d)"

meter-test: $(ROM_NOMETERS)
	@echo "wrote $(ROM_NOMETERS) (NM top-bar marker; channel meters disabled)"

# Deeper sample-timing diagnostic: in addition to suppressing meter redraws,
# remove DAC peak measurement from both timer-IRQ hot paths and the engine-tick
# snapshot. "NP" distinguishes it from the normal and NM-only ROMs.
$(ROM_NOPEAKS): $(SRC_C) $(SRC_S) src/tracker.h $(CFG) $(BUILD)/font.h $(BUILD)/logo.h $(BUILD)/notes.h $(BUILD)/buildid.h $(SAMPLE_BANK) $(SAMPLE_BANK_FORCE)
	cl65 -t lynx -O -D ALYNXDJ_NO_METERS=1 -D ALYNXDJ_NO_DAC_PEAKS=1 --asm-define ALYNXDJ_NO_DAC_PEAKS=1 -C $(CFG) -m $(BUILD)/alynxdj-no-meters-no-peaks.map -o $@ $(SRC_C) $(SRC_S)
	$(PYTHON) -c "p='$@'; d=bytearray(open(p,'rb').read()); d[60]=5; open(p,'wb').write(d)"
	$(PYTHON) tools/alynxdj_pool.py --validate $(SAMPLE_BANK) --capacity $(SAMPLE_POOL_CAPACITY)
	$(PYTHON) -c "\
p='$@'; d=bytearray(open(p,'rb').read()); \
aux1=open('$(BUILD)/aux1.bin','rb').read(); \
aux2=open('$(BUILD)/aux2.bin','rb').read(); \
aux3=open('$(BUILD)/aux3.bin','rb').read(); \
mid=open('$(BUILD)/auxmidi.bin','rb').read(); \
assert len(d) <= 64+40*1024, 'program spills into the RAM overlay blocks'; \
assert len(mid) <= 0x600, 'live helper overlay overflow'; \
assert len(aux1) <= 0x700 and len(aux2) <= 0x600 and len(aux3) <= 0x2E0, 'RAM overlay overflow'; \
d += b'\0'*(64+40*1024-len(d)); d += aux1; \
d += b'\0'*(64+42*1024-len(d)); d += aux2; \
d += b'\0'*(64+44*1024-len(d)); d += aux3; \
d += b'\0'*(64+45*1024-len(d)); \
d += open('$(SAMPLE_BANK)','rb').read(); \
d += b'\0'*(64+254*1024-len(d)); d += mid; \
d += b'\0'*(64+256*1024-len(d)); \
open(p,'wb').write(d)"

sample-timing-test: $(ROM_NOPEAKS)
	@echo "wrote $(ROM_NOPEAKS) (NPxx marker; xx is the live DAC-underrun count)"

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
	$(PYTHON) -c "from PIL import Image; Image.open('$(BUILD)/shot.ppm').save('$(BUILD)/shot.png')"
	@echo "wrote $(BUILD)/shot.png"

test-bank: $(ROM)
	$(PYTHON) tools/test_sample_bank.py $(ROM) $(SAMPLE_BANK) sample-patch-browser.html $(BUILD)/auxmidi.bin

test-dac: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_dac_symmetry.py $(RETROSHOT) $(EMUCORE) $(ROM)

test-save: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_save_roundtrip.py $(RETROSHOT) $(EMUCORE) $(ROM)

test-tone: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_tone_modulation.py $(RETROSHOT) $(EMUCORE) $(ROM)

test-editor: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_editor_clone.py $(RETROSHOT) $(EMUCORE) $(ROM)

test-hardware: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_hardware_fixes.py $(RETROSHOT) $(EMUCORE) $(ROM)

test-midi: $(ROM) $(RETROSHOT)
	$(PYTHON) tools/test_midi_takeover.py $(RETROSHOT) $(EMUCORE) $(ROM)

test: test-bank test-dac test-save test-tone test-editor test-hardware test-midi

# Companion USB-MIDI-device firmware. Requires a complete Arm embedded
# toolchain and PICO_SDK_PATH; it is intentionally not part of the ROM build.
pico:
	cmake -S pico-midi-comlynx -B $(BUILD)/pico-midi-comlynx -DPICO_BOARD=pico
	cmake --build $(BUILD)/pico-midi-comlynx
	cp $(BUILD)/pico-midi-comlynx/alynxdj_midi_comlynx.uf2 $(BUILD)/alynxdj_midi_comlynx.uf2
	@echo "wrote $(BUILD)/alynxdj_midi_comlynx.uf2 (Raspberry Pi Pico / RP2040)"

# version-only release copy (attach these to GitHub releases)
dist: $(ROM)
	cp $(ROM) $(BUILD)/alynxdj_$(subst .,_,$(VERSION)).lnx
	@echo "wrote $(BUILD)/alynxdj_$(subst .,_,$(VERSION)).lnx"

clean:
	rm -rf $(BUILD)

.PHONY: all factory-samples meter-test sample-timing-test shot test test-bank test-dac test-save test-tone test-editor test-hardware test-midi pico dist clean
