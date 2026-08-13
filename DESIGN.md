# ALYNXDJ — an LSDJ-inspired tracker for the Atari Lynx

The design contract. Read the relevant section before making design decisions;
don't re-litigate settled ones — when a decision must change, change it
deliberately and update §0. The proven ancestors are **SMSGGDJ**
(`/Users/a1106632/Documents/sms_tracker`) and **GENMDDJ**
(`/Users/a1106632/Documents/genmddj`); where a behaviour below is marked
*ported*, their DESIGN.md / source is the reference implementation.

---

## 0. Decision log (load-bearing forks)

| D | Decision | Alternative (documented, rejected) |
|---|---|---|
| D1 | **4 identical tracks (CH1–CH4), every track plays every instrument type** (LFSR/WAV/KIT; see D35). The Lynx has no fixed channel roles — that *is* its pitch. | Fixed roles (3 tone + 1 noise, SMS-style): simpler arbitration, wastes the hardware's symmetry. |
| D2 | **cc65 C for editor/UI/data, ca65 assembly for the sound driver, PCM IRQ, and text render.** cc65 codegen is slow but the editor workload is small at 160×102; the audible paths stay in asm. | All-asm (sibling style): faster everywhere, much slower to build; revisit per-routine if profiling demands. |
| D3 | **Engine tick = VBlank at ~59.9 Hz** (measured at M1: the cc65 crt0 display timing, 159 µs/line × 105 lines = 16.695 ms — Handy's 75 fps `retro_run` pacing is a frontend artifact, the emulated timer 2 runs at 59.9 Hz). Groove/BPM tables use the siblings' NTSC-60 math near-verbatim. No PAL/NTSC split exists on Lynx — single tables. | Reprogram timer 0/2 for a 75 Hz display (many Lynx games do): more tick resolution, breaks BPM-table parity with the siblings, LCD flicker tradeoffs — only revisit with hardware in hand. |
| D4 | **Song lives as one contiguous flat RAM block (save image order), but the EEPROM save is a packed/RLE serialization of it.** The Lynx persists to a 93Cxx serial EEPROM — 2 KB max (93C86) — so the siblings' verbatim-copy save cannot survive. Flat block keeps the engine + tooling model; the codec is the one new layer. | Shrink the song to fit 2 KB raw: guts the data model. RAM-only + ComLynx export only: no self-contained save on cart. |
| D5 | **Samples are 8-bit signed PCM streamed from cart; the owning track selects its own physical channel DAC.** KIT and table-WAV can therefore play on any of channels A–D, matching Mikey's symmetry. Direct 8-bit DACs × 4 are the Lynx's sample superpower (vs the SMS 4-bit log trick, vs GENMDDJ's single shared YM2612 DAC). | Fixed sample buses on C/D: simpler, but contradicts D1 and wastes Mikey's per-channel DACs. Software-mix N samples into one channel: more polyphony, too much hot-loop work. |
| D6 | **Timer-fed sampled voices are capped at 2 across KIT + table-WAV.** Timer 7 feeds slot 0, timer 5 feeds slot 1; each slot dynamically targets its owning track. A third trigger steals the oldest sampled voice. | Four concurrent streams because the chip has four DACs: hardware-valid, but the timer-IRQ CPU load would eat the editor and jitter the engine tick. |
| D7 | **4×6 pixel font, 40×17 character grid** (160×102). 8×8 gives only 20×12 — no room for a 16-row phrase grid + chrome. Hex-heavy glyphs designed first, sibling discipline. | 8×8 font (sibling look): too few rows; a paged UI would break the LSDJ feel. |
| D8 | **Stereo is written unconditionally (ATTEN/PAN/MSTEREO), audible on Lynx II, harmless on Lynx I (mono).** Same one-song-pans-where-panning-exists policy as the SMS/GG `O` split. | Gate on `MIKEYREV`: adds a probe for zero benefit — the writes are ignored on rev 1. |
| D9 | **Software text render into a single 8160-byte framebuffer; no Suzy blitter, no double buffer.** A text grid updates a few cells per frame; VBlank-window writes avoid visible tear. | Suzy sprite font: hardware-fast but drags in SCB management + Suzy/CPU bus handover for a workload that doesn't need it. |
| D10 | **The working song stays full-size in RAM; SAVE packs into EEPROM with a live "fits/doesn't" size meter** on FILES. A song too big to save is the user's tradeoff, surfaced honestly. | Hard-cap song size to guaranteed-packable: punishes every song for the worst case. |
| D11 | **The 12-bit LFSR is fully exposed per instrument: a raw 9-bit TAPS mask + a 12-bit SEED**, replacing curated timbre presets. User encoding is contiguous (bits 0-5 = taps 0-5, 6 = tap 7, 7 = tap 10, 8 = tap 11) so value-sweeping walks musically adjacent configs; the engine remaps to the scattered FEEDBACK/control layout at trigger. The seed picks the state-graph *cycle* — for many tap sets different seeds are genuinely different waveforms (verified: taps $0F1 seeds 000/555 → spectral cosine 0.05), and some (taps, seed) pairs hit the lock state and go silent, like the silicon. Short tone and long noise timbres are regions of one continuous LFSR instrument (D35). | Preset banks only (the M6 design): safe but hides the instrument that makes the Lynx unique. |
| D12 | **One save slot: the packed song owns the whole 2,032-byte EEPROM payload** (cells 1020–1023 stay reserved for machine config). FILES manages the working set instead: NEW (confirmed wipe), DEMO (confirmed factory-song load), and PURGE (drop unreferenced chains/phrases to shrink the pack). A boot with no valid save starts clean; DEMO is never loaded implicitly. Off-cart backup is ComLynx's job (a future SEND/RECV pair — 2 KB moves in ~0.3 s at 62.5 kbaud); emulator users swap `.eeprom` files for free slots. | Multiple slots (SMSGGDJ has 6 in 32 KB SRAM): halving 2 KB caps every song under half capacity — the demo alone packs ~1.2 KB. Revisit only if a real flashcart offers bigger persistent storage. |
| D13 | **One global groove, no pool.** The GROOVE screen edits a single groove that sets tempo + swing for the whole song; PROJECT's TMPO steps its entries together. The `G` (switch-groove) command is dropped from the editor menu (its CMD id is retained so every later command keeps its save encoding, and the engine executor is self-limiting to groove 0). | A 16-groove pool + `G` per-phrase (LSDJ/SMSGGDJ model): more rhythmic variation, but the pool UI and a live-mutable tempo source are complexity the Lynx build doesn't need — one swing feel per song is the common case. |
| D14 | **`G` repurposed as a live tap glide** (the retired groove-switch slot, D13). Its signed byte gives direction plus a hybrid tick/row period: positive moves upward, negative downward, magnitudes 1–7 count ticks, and magnitude 8 begins the row-locked range (D18). Live writes change FEEDBACK/control without reseeding the shifter, so the LFSR keeps running while the 9-bit value wraps. Reuses CMD_G's id so every other command keeps its save encoding. | A brand-new command letter/id: cleaner semantically, but grows the command set and leaves a dead reserved slot. Reusing G keeps the count flat. |
| D15 | **LFSR instruments own persistent SWP/VIB/TRM modulation.** The three formerly-reserved record bytes hold signed 1/16-semitone-per-tick sweep plus packed speed/depth vibrato and tremolo. The legacy stored type `$01` shares this surface because it aliases the same Mikey polynomial oscillator; WAV/KIT show the fields disabled. Save v4 clears the new bytes when loading older songs. | Commands only (`P`/`V`) and tables: capable, but forces routine patch identity into phrase data and leaves no persistent tremolo. Applying the fields to sampled voices: their rate/volume paths have different costs and semantics. |
| D16 | **Instrument byte 15 is signed TSP for every type; vibrato is a centred, transport-scoped sine capped at ~7.5 Hz.** TSP resolves once at key-on before LFSR/WAV pitch or KIT pad mapping and clamps at the playable limits. The 16-point sine uses an 8-bit phase accumulator with 16 distinct ~0.47–7.49 Hz rates, so even the maximum has eight engine updates per cycle. Its depth follows SMSGGDJ's nonlinear `0,1,2,3,4,5,6,8,10,13,17,22,28,36,46,60` response in 1/16-semitone units (up to ±3.75 semitones), but stays symmetric and key-independent. Phase free-runs across note retriggers and resets at the transport boundary; depth zero neither changes pitch nor advances it. Save v5 clears the formerly-reserved TSP byte on older songs. **D46 supersedes only the KIT interpretation of byte 15; LFSR/WAV and the VIB design remain unchanged.** | LFSR-only transpose: inconsistent with the sibling instrument model and leaves pitched WAV/KIT selection behind. A faster sine: at the 59.9 Hz engine tick it would still staircase/alias. Per-note phase reset: deterministic, but short notes repeatedly sample one half-cycle and acquire a tuning bias. Raw-period depth like SMSGGDJ: its musical interval changes with the played note and is asymmetric around the centre pitch. |
| D17 | **Instrument TBS occupies HOLD's formerly-unused high nibble, and every table clock loops the full 16 rows by default.** TBS 0 advances the attached table once per triggered note without resetting its row; TBS 1–F restart at row 0 and advance every N engine ticks. Both modes wrap `0F→00`, exactly like SMSGGDJ/GENMDDJ; `Hxx` defines a shorter or non-zero loop point. The live row byte packs its countdown above the 0–15 playhead, so the 16-byte instrument record, voice RAM, and 2 KB persistence ceiling do not grow. Save v6 assigns the nibble; older writers already canonicalized it to zero. | Add instrument/voice bytes: clearer but impossible in the fixed save/RAM budgets. Always one row per tick: too fast and loses the sibling trackers' note-clocked table mode. Stick at `0F` unless `H` is present: diverges from both sibling engines and turns a full-length macro into a one-shot. |
| D18 | **Tap motion has complementary track accumulators.** Each `G` first restores the active instrument's stored TAPS, then treats its signed byte as direction plus a split period. Absolute magnitudes 1–7 are tracker ticks (`G01` = +1/tick, `G07` = +1/7 ticks; `GFF` through `GF9` are the downward equivalents). Magnitude 8 begins a row-locked range whose period is magnitude−7 (`G08` = +1/row, `G0B` = +1/4 rows; `GF8` and `GF5` are the downward equivalents); `G00` resets and stops. The command begins a complete first period at the original value, so `G07` first changes after seven ticks and `G0B` at row +4. Tracks own independent countdowns, and ordinary notes preserve the live value plus partial countdown. Nonzero `Bxx` likewise adds its signed byte to the current live TAPS and arms cumulative track state across later notes and structural boundaries; successive `B01` commands therefore add +1 each. `B00` restores the active instrument's TAPS and releases that state, while transport stop and a new `G` also clear it. Both commands update FEEDBACK and the scattered tap-7 control bit without reseeding. | Keep `B` as a live WAV-bank command: useful but narrow, while WAV bank is already persistent per instrument and hardware listening called for finer LFSR control. Make B note-local: repeated notes silently restore the patch and defeat its automation role. Let consecutive G commands continue from the live value: useful for chaining rates, but makes it difficult to repeat a deterministic timbre sweep. Reset G on every note: deterministic per-note timbre, but destroys slow automation. Tick-only G: cannot offer very gradual, row-locked motion. Row-only G: cannot reach modulation-like rates. |
| D19 | **ComLynx MIDI takeover is an exclusive receive-only live mode:** normalized full-status MIDI messages map channels 1–4 to tracks A–D and zero-based instruments 00–03 through the ordinary trigger path. Notes are monophonic per track; Note Off enters patch DECAY (DCY 0 cuts), KIT stops, velocity is carried but only zero/nonzero is used, and CC120/123 plus `$FF` provide release/panic. There is deliberately no heartbeat or link timeout. A timer-4 RX IRQ feeds a 64-byte ring overlaid on the stopped sequencer's phrase counters. The ring is drained at the start of the 59.9 Hz engine tick, where normal tracker triggers can run without masking nested UART receive; this batches simultaneous channels, adds bounded 0–16.7 ms trigger quantization, and makes MIDI independent of redraw work. The cold MIDI parser lives in the EEPROM pack workspace and is reloaded after SAVE/LOAD, retaining the complete song, 2,032-byte save, 512-byte stack, and two 512-byte PCM rings. | Raw USB-MIDI interpretation directly in the editor/frame poll: would overrun Mikey's one-byte UART during chords and lets redraw defer note application. A second MIDI sound path: duplicates patch behaviour and breaks track symmetry. Heartbeat timeout: useful later, but premature until the real bridge/cable establishes actual failure behaviour. |
| D20 | **MIDI clock is divided to tracker rows by the Pico:** Start/Continue is the downbeat, so `FA`/`FB` is immediately followed by one row-rate `F8`; after resetting phase, every six later source `F8` clocks produce the following row grant for the Lynx's distinct `IN24` mode. `FA` Start supplies a row-0 cue only when the user has not already armed a local row; `FB` currently has the same behavior because Song Position Pointer is not implemented; `FC` Stop halts. `IN` and `IN24` show `WAIT` after local transport start, then present the cued row and change to `PLAY` on the first row pulse. The original `01/02/03` `IN` protocol remains Lynx-to-Lynx compatible. Keeping the bridge pulses as MIDI real-time bytes lets them safely share one stream with complete channel messages: `MIDI` ignores clock and `IN24` ignores notes. The companion firmware is a USB-MIDI *device* plus D52's opto-isolated serial-MIDI input, using RP2040 PIO for exact 62.5-kbaud open-drain ComLynx framing and having no heartbeat. | Wait for six clocks after Start before the first grant: introduces exactly one tracker row of startup latency. Divide on the Lynx: spends scarce Lynx code/state on a job the bridge already understands. Reuse legacy `IN` for the mixed stream: its low `01/02/03` opcodes can also occur as MIDI data and cause false row/transport events. Make the first bridge a USB host: materially different USB power/topology work; defer until a directly attached controller is required. |
| D21 | **Command selection is alphabetical, context-valid, and independent of storage ID:** PHRASE traverses `A B C D E F G H I J K L N O P R S T V W X Z`; TABLE traverses only `B C E F G H K N O P R S T V W X`, skipping `A D I J L Z` because its macro engine has no table-switch, trigger-delay, phrase-pass, note-slide, or note-gate context for them. Empty sits between each context's endpoints in both directions, and global command recall rejects the same invalid TABLE set. The editor uses an explicit ID mapping so historical command bytes and save format remain unchanged. | Renumber command IDs alphabetically: simpler display logic, but silently changes every existing phrase/table command in saved songs. Present commands that TABLE ignores: suggests useful automation but silently does nothing. Keep chronological implementation order: save-safe, but needlessly awkward to browse. |
| D22 | **HOLD `F` is the sole indefinite patch sustain; `0`–`E` retain timed meanings, and DCY `0` is immediate.** AHD `0/0/0` produces one audible engine tick (~16.7 ms) and then reaches zero. HOLD F remains at peak until the channel retriggers, `K` kills it, transport stops, or live MIDI supplies Note Off. This changes playback semantics without changing the instrument record or save-format v6. | Treat DCY 0 as sustain forever: makes the all-zero envelope unexpectedly infinite and duplicates HOLD F. Keep `F` as a finite 15-tick hold: provides no direct patch-level sustain control. Add a separate gate flag: clearer, but there is no spare persistent instrument bit. |
| D23 | **Instrument TRM is a unipolar descending 6-bit saw.** Each note starts at the live AHD level, ramps downward according to depth, and snaps back to the top every cycle. The existing speed range (~0.94–14.98 Hz), per-note phase reset, packed speed/depth byte, and save v6 representation stay unchanged. At deep settings with HOLD `F`, the snap supplies a cheap repeating-decay/echo-like envelope. | Triangle attenuation: conventional tremolo but spends half of every cycle rising gradually and is less useful as a rhythmic repeat envelope. Centred modulation: would need headroom or clipping and could exceed the instrument's programmed peak. |
| D24 | **PHRASE entry has an explicit instrument latch and field-safe command double-taps.** Editing any PHRASE instrument number updates a runtime latch (initially `00`); every later note placed into an empty row inherits it, while viewing/pasting rows does not change it. A B double-tap on CMD or PARAM can affect only that pair: it pastes a command clipboard or otherwise clears CMD+PARAM, never NOTE+INSTR. | Derive each new note from the destination row's zero/default instrument: forces repetitive instrument entry. Update the latch from every visited or pasted row: makes note entry context-dependent and unpredictable. Let generic step paste run from command columns: a stale empty-row clipboard can erase the note, which is too destructive for field-level editing. |
| D25 | **The 64-slot `PL` sample pool is a portable sample-bank binary, and the shipping ROM remains 256 KB for now.** `samples/alynxdj-factory-samples.bin` is injected verbatim at block 45; the browser imports/exports the same format, provides non-destructive waveform IN/OUT trimming, and `SAMPLE_BANK=...` selects another bank at build time. The browser always exposes the software's zero-based KIT `00`–`07` even when the source bank declares fewer kits; missing kits appear empty and export as one-byte silent pads unless filled, subject to the ordinary capacity check. After D31, blocks 45–249 provide 209,920 bytes total; HELP owns 250–253 and the MIDI helper remains at 254–255. Each directory length is u16, so one sample may use 1–65,535 bytes (~12.58 s at D39's rate), subject to the shared bank cap. Keep 8 kits × 8 pads because that mapping is already native to KIT notes, instruments, UI, and songs. A 512 KB `.lnx` uses 2 KB rather than 1 KB cart blocks, so adopting it is a separate addressing/layout migration, not padding; defer it until real custom banks exceed the current cap. | Generate samples from WAV on every ROM build: makes custom sets release-coupled. Add more than 64 slots immediately: requires new bank/pad selection semantics for little benefit. Switch to 512 KB now: doubles space but changes every cart block and offset before current banks need it. |
| D26 | **Channel activity meters are removed from the ROM.** Sample integrity, tempo, redraw cadence, and input latency take priority over decorative metering. In particular, KIT/WAV peak measurement no longer runs in the 7.8 kHz/variable-rate DAC interrupt, and the right edge is left available to the screen-map chrome. Internal saturating underrun counters remain for regressions. | Keep meters but rate-limit their rendering: saves framebuffer work, but leaves peak accounting in the hottest audio path or replaces it with a less truthful proxy. |
| D27 | **Framebuffer rendering is cooperative with cart-streamed audio.** A full screen change may span several VBlanks, so the renderer services pending KIT triggers and both PCM rings at a bounded four-glyph interval; the 7.5 KB grid clear is split into sixteen six-pixel bands with a service point after each. Refills wait for at least one efficient 64-byte piece of free ring space, and a same-track KIT retrigger leaves the old sample running until the replacement directory entry and startup cushion are ready. The VBlank sequencer remains authoritative for row timing while the foreground cannot monopolize sample preparation. | Make every screen an incremental multi-frame state machine: gives an even harder budget but complicates cursor/playhead redraw ordering and makes screen transitions visibly assemble. Let the interrupt read the cart: would make the hottest audio path unbounded and unsafe. |
| D28 | **A clean Option-1 tap toggles contextual all-track transport; the held layer is unchanged.** While stopped, SONG starts all tracks at the selected song row; CHAIN additionally applies its selected chain position; PHRASE and its descendant screens additionally apply the selected phrase row. A shorter parallel chain falls back to its first phrase. `IN`/`IN24` starts arm as WAIT. While playing or waiting, a clean tap stops immediately instead of restarting. Using Option 1 with left/right, B, or A still selects a track, mutes, or solos and suppresses the tap action. | Extend physical A+B preview to all tracks: breaks its useful selected-track CHAIN/PHRASE audition behavior. Make Option 1 transport only: loses the established mute/solo layer. |
| D29 | **Phrase `Hxx` branches before its marker row and SONG loops are per contiguous vertical group.** The H row's note and other fields are never executed. In a CHAIN/SONG/LIVE context, `H00` is an early phrase boundary: the next chain phrase begins immediately, while a one-phrase chain naturally loops to its first phrase; standalone PHRASE audition retains the local row-00 loop. `H01`–`H0F` remain local row branches. Table H keeps its table-loop semantics. In arrangement mode an empty SONG cell is a hard delimiter for that track: reaching the bottom of a non-empty run loops to that run's own top and never falls through to an earlier disconnected group. | Execute H and then end: costs an audible marker row. Always interpret H00 as a local row loop: prevents short phrases from advancing through a chain. Globally wrap to the first song group: makes later arrangement sections impossible to cue and loop independently. |
| D30 | **The meter-free UI gives its horizontal space back to content and makes pending LIVE intent explicit.** Every editor screen body except the full-width WAVE and HELP views is shifted eight character columns right; the top bar and map remain fixed. LIVE queues render the destination chain in inverted accent, while a queued stop renders inverted `ST` on the armed empty cell. A clean/held-release physical-A action is inert on INSTR (A-held directions still navigate). D45 supersedes the original local TABLE-command inheritance rule with one global PHRASE/TABLE command latch. | Leave the body hard-left: preserves coordinates but wastes the newly opened gap beside the map. Represent start and stop with the same blank/inversion: hides what will happen at the boundary. |
| D31 | **HELP is a read-only, data-driven screen above TABLE.** `help.txt` is validated and packed at build time into nine ordered pages; headings invert, the title shows `n/9`, ordinary D-pad directions turn pages with wraparound, physical-A-held + Up enters from TABLE, and A-held + Down returns. A-held + Down remains the TABLE-number decrement, while Up is now the HELP route. The full-width HELP body hides the map after entry. Blocks 250 and 251–253 respectively hold its cold renderer and text. Entering HELP stops transport, then loads the renderer over the idle `$D000-$D3FF` PCM rings; FILES → PURGE, which already stops transport, shares the same cold overlay. This spends no song RAM or resident code region and a later DAC trigger refills its ring normally. | Hard-code prose into editor C: makes wording changes consume the already-exhausted resident code/RAM budget. Keep HELP/PURGE resident: displaces song/audio state. Let HELP coexist with active PCM: its code and the DAC rings would overwrite one another. |
| D32 | **Hierarchy entry and command deletion are explicit editor actions.** Drilling SONG→CHAIN or CHAIN→PHRASE always lands the child cursor on row `00`, eliminating stale per-screen cursor positions while still selecting the chain/phrase under the parent cursor. INSTR has a selectable `INSTR 00–1F` field above TYPE; changing it repaints and edits that patch without altering the phrase row or insertion-instrument latch. On TABLE's command-letter column, physical-B-held + A clears only CMD+PARAM, preserving VOL+TSP just as PHRASE preserves NOTE+INSTR. | Preserve each child cursor across re-entry: occasionally useful, but appears random when entering a different object. Require leaving INSTR to choose another patch: needlessly breaks patch comparison. Treat a TABLE command delete as a whole-row clear: too destructive. |
| D33 | **TABLE has a selected-track playhead.** While the table being viewed is active on the track selected by the top-bar `T1`–`T4` control, its current macro row number uses the same accent as CHAIN/PHRASE playheads. An inactive selected voice or a voice running another table shows no marker. The editor derives this from the existing voice table/cursor state through a compact assembly reader, so the engine tick gains no state or work. | Show all four tracks at once: ambiguous when several tracks share a table and needs four visual lanes. Follow whichever track changed most recently: makes the marker jump independently of the explicit track selector. Add per-tick UI state: wastes RAM and work in the timing-critical engine. |
| D34 | **INSTR shows and visits only parameters used by the selected type.** LFSR exposes VOL/AHD, TSP, SWP/VIB/TRM, TAPS/SEED, PAN, TABLE and TBS; WAV exposes VOL/AHD, TSP, WAVE, PAN, TABLE and TBS; KIT exposes VOL, TSP, KIT and PAN alongside the universal INSTR/TYPE selectors. Cursor movement skips the omitted field IDs, eliminating invisible navigation stops. | Render disabled `--` rows and leave blank selector rows in the cursor cycle: preserves fixed coordinates, but implies unsupported controls and creates invisible navigation stops. Show KIT's unused AHD/table surface: implies real-time shaping that its streamed one-shot path does not provide. |
| D35 | **LFSR is the single user-facing Mikey polynomial instrument.** TYPE cycles `LFSR → WAV → KIT`; new songs and the demo use stored ID `$00`. Historical NOISE ID `$01` remains an invisible, byte-compatible LFSR alias: it displays the same name and fields, uses the same engine path, and retains its TAPS, SEED, modulation, and exact sound when old saves are loaded. The editor and SRAM browser never newly select `$01`, while WAV `$02` and KIT `$03` keep their established IDs, so save format v6 does not change. | Keep separate TONE and NOISE labels: suggests two hardware engines when both are merely different tap/seed regions of the one LFSR. Renumber WAV/KIT to make IDs contiguous: would corrupt existing songs and external tools. Rewrite `$01` on load: unnecessary mutation of otherwise valid old save data. |
| D36 | **KIT VOL is a static four-state PCM gain implemented in the foreground ring fill.** Only the existing VOL byte's high nibble is meaningful: `6-`/`7-` leaves samples unchanged, `2-`–`5-` applies signed `>>1`, `1-` applies signed `>>2`, and `0-` mutes. INSTR blocks the ignored fine digit with `-`; Left/Right are inert and Up/Down select `0-`–`7-`. Gain is latched at note or `R` retrigger, each 64-byte cart piece is shifted before atomic publication, and the timer/DAC interrupt remains unchanged. Trigger startup begins after a 256-byte cushion and the caller immediately tops up the ring, preserving redraw-safe start timing. Save format remains v6 and the low nibble is preserved until a coarse KIT edit. | Multiply every DAC byte for smooth 0–127 gain: too expensive in the hottest interrupt. Expose an editable fine nibble that does not affect sound: misleading UI. Replace per-instrument VOL with permanent source scaling: prevents per-instrument levels. Add a KIT envelope: needs continuous rescaling or another mixing path and misrepresents a cheap one-shot control. |
| D37 | **Block SELECT highlights every rendered field on each selected SONG/CHAIN/PHRASE row.** The visible range is published in two transient helper-window bytes and the shared glyph renderer applies one inverse-accent treatment to row indices and data cells alike. Copy/cut boundaries and the save format are unchanged. | Invert only row indices: compact but makes a multi-row selection unnecessarily hard to scan. Add selection branches to every hierarchy renderer: duplicates behavior in three already space-constrained resident paths. |
| D38 | **KIT bank is a total `00`–`07` selector with default `00`.** Selecting KIT normalizes the shared WAVE/KIT byte to `00` if it was unset, viewing an old KIT patch repairs the same invalid state, and decrementing `00` clamps there. WAV alone retains `$FF`/`--` for its hardware integrate triangle. The ROM editor and standalone SRAM viewer enforce the same rule without changing the 16-byte instrument record or save-format v6. | Let KIT inherit WAV's `--`: describes no playable bank and makes a newly changed KIT patch appear incomplete even though the engine already falls back to kit 00. |
| D39 | **KIT banks use one canonical 5,208.333 Hz signed-PCM rate.** Mikey's 1 MHz timer clock uses reload 191, cutting each KIT feeder's interrupt rate by one third versus 7,812.5 Hz while increasing a u16 slot's maximum duration to ~12.58 s. `PL` header byte 3 is rate ID `1`; the builder and standalone browser reject other IDs instead of silently changing pitch, and every KIT stream keeps reload 191 (D47 supersedes the former raw-timer `S`). Factory conversion accepts 8/16/24/32-bit integer WAV PCM, normalizes to mono signed 8-bit, and writes this format. Compatibility with experimental older-rate banks is deliberately not carried: custom sources should be reconverted or repatched. | Keep 7,812.5 Hz: more top-end, but the measured two-voice IRQ/redraw margin is the limiting hardware resource. Runtime multiple-rate support: consumes scarce code/state and makes portable-bank behavior ambiguous before there is a user base to preserve. |
| D40 | **Factory WAV conversion is mastered through +12.00 dB of tanh drive.** After mono conversion, resampling, silence trim, and the existing 120/127 peak normalization, the builder applies a 3.981× gain followed by `tanh`, quantizing the bounded result to signed 8-bit. This is baked into the portable factory bank and costs nothing during Lynx playback; KIT VOL still supplies per-instrument attenuation. The standalone browser retains independent gain/drive controls for custom bank work rather than silently remastering an already packed bank. | Hard-clip after +12 dB: loud but produces harsher flat-topped distortion. Normalize only: leaves most sample bodies substantially quieter. Apply tanh in the DAC feeder: wastes real-hardware CPU on invariant work and worsens the timing problem the lower rate solved. |
| D41 | **`Jxy` uses the sibling trackers' mask-first order.** High nibble `x` is a four-bit phrase-pass mask indexed by `(play count mod 4)`; on a selected pass, low nibble `y` transposes this row's note as a signed value from −8 to +7. Thus `J17` is +7 on the first of four passes and `JF2` is +2 on every pass. This corrects ALYNXDJ's original reversed interpretation without changing the command ID or save layout; existing ALYNXDJ J parameters must have their nibbles swapped once. | Retain transpose-first `J71`: internally consistent with the early ALYNXDJ manual, but needlessly breaks song-data parity with both SMSGGDJ and GENMDDJ. |
| D42 | **Stereo level is a universal patch field plus the existing `Oxy` live command.** Every LFSR/WAV/KIT instrument exposes its existing byte-7 PAN as left/right level nibbles (`0` mute, `F` full), default `FF`; Up/Down edits the left nibble and Left/Right edits the right nibble independently. `Oxy` writes the same live per-voice level and remains in effect until the next note restores the active instrument's PAN. The shared WAVE/KIT selector moves to the first modulation row omitted by those types, while PAN uses LFSR's formerly blank BANK row, keeping the 15-row form and save-format v6 unchanged. Writes remain unconditional: stereo is audible on Lynx II and harmlessly mono on Lynx I (D8). | Add a second panning command: conflicts with the complete alphabetical command set and duplicates `O`. Store a single left/right balance value: simpler, but cannot express centre attenuation, dual-mono level, or asymmetric levels as directly as Mikey's two ATTEN nibbles. |
| D43 | **The standalone sample patcher can slice one unnormalized source into a kit.** The single HTML file directly parses PCM or floating-point RIFF/WAVE metadata, mixes channels, and performs band-limited conversion from the file-declared rate to 5,208.333 Hz; source duration and pitch therefore do not depend on an AudioContext's implicit resampling, while out-of-band energy is filtered instead of folding into false low tones. The floating-point result supplies user-selected IN/OUT, 1–8 equal divisions, and a unique destination pad for each slice. Every numbered waveform region is itself an audition/stop control with a visible playing state, while the mapping cards retain equivalent accessible buttons. One shared gain stage feeds optional tanh before signed 8-bit quantization, preserving relative level and exact boundary continuity when fade is off. Optional 1/2/5/10 ms linear end fades reach zero on every slice for independent one-shot click control and default off because they intentionally break continuous boundaries. Per-slot u16 and complete-pool capacity are validated before any pad is replaced. | Normalize each slice: makes every pad loud but destroys the dynamics and continuity of a sliced phrase. Trust browser AudioContext conversion plus point sampling: can obscure the source rate and alias rejected high frequencies into misleading low pitches. Always fade: hides end clicks but inserts unwanted dips when slices are chained. Quantize before shared processing: loses useful resolution in quiet long-form sources. |
| D44 | **A zero PAN nibble is enforced through both ATTEN/MPAN and MSTEREO.** Each PAN or `Oxy` write still programs the later Lynx II 4-bit level register, but a zero left/right nibble also sets that channel-side's older MSTEREO disable bit. Thus `00` is an exact mute and `F0`/`0F` are exact hard pans even on stereo implementations that honor channel switching but ignore fractional attenuation; nonzero `1`–`F` levels retain their full resolution where ATTEN/MPAN exists. The four-track disable shadow lives in zero page and is updated only when PAN is applied, not in the tick or DAC IRQ. | Trust ATTEN/MPAN alone: correct in Handy and full panning hardware, but real-hardware listening found units/paths where `00`, `F0`, and `FF` were indistinguishable. Use MSTEREO for every non-full value: makes endpoints work but throws away the later hardware's useful intermediate levels. |
| D45 | **Command recall is global, phrase `A` owns table override, and editor entry/envelopes are deterministic.** Changing a command letter or parameter in either PHRASE or TABLE remembers the complete pair; tapping physical B on an empty command-letter cell inserts both bytes, while occupied cells remain unchanged and TABLE rejects its D21-invalid command set. Phrase `Axx` is resolved before note trigger: changing targets restarts the selected table, but repeated selection of the same table preserves its cursor so TBS 0 advances once per note. Entering INSTR from a PHRASE note always selects that row's valid instrument and lands on the top INSTR selector. DCY 0 reaches zero immediately after the trigger tick; HOLD F is the only indefinite patch sustain (D22). | Remember commands per table: prevents a useful phrase-to-table workflow and needs a scan or extra state. Execute A from inside a table: recursive table switching with no sibling precedent. Restart every A target at row 0: makes TBS 0 appear broken. Preserve the stale INSTR field cursor: makes entry appear random. |
| D46 | **KIT reuses instrument TSP as a fixed-IRQ source-step RATE.** `$FF` repeats each signed PCM byte once for 0.5×, `$00` consumes every byte for 1×, `$01` advances two bytes (output one/skip one) for 2×, and `$02` originally advanced four for 4×; D48 supersedes that sparse stored-value mapping with dense 1×–4× values. Mikey stays at D39's 5,208.333 Hz timer rate, so faster source stepping does not multiply IRQ frequency. D48's six-piece service limit makes trigger prefill 384 bytes, superseding D36's 256-byte cushion. The ring's existing WAV position/step bytes are reused as KIT phase/stride because a DAC slot cannot be WAV and KIT simultaneously, adding no RAM. Every note and `R` retrigger restores patch stride plus reload 191; D47 defines `Sxx` as a bounded live stride override. Instrument TSP no longer changes KIT pad selection, while chain/phrase transpose still changes the note and can therefore select another pad. LFSR/WAV keep signed-semitone TSP, the 16-byte record and save-format v6 stay unchanged, and the portable bank remains canonical-rate PCM. | Raise the timer IRQ for higher rates: preserves every source byte but recreates the hardware slowdown this rate control is meant to avoid. Resample a new bank for every rate: costs ROM and prevents live patch changes. Interpolate half-speed samples: smoother but adds arithmetic to the hottest path. Keep KIT transpose: useful for pad remapping, but chain/phrase transpose already provides it and a cheap instrument-rate control adds a genuinely new sound dimension. |
| D47 | **`Sxx` is a bounded KIT source-speed override, never a timer reload.** The low two bits select `0`=1×, `1`=2×, `2`=4×, or `3`=0.5×, matching the siblings' four DAC-walk states; higher command values therefore fold safely onto one of those rates. S updates an already-playing KIT immediately and also updates a same-row pending trigger, while the next KIT note or `R` retrigger restores the instrument's TSP/RATE. LFSR and WAV ignore S. Mikey's KIT timer remains reload 191 at 5,208.333 Hz in every state, so command automation cannot recreate the excessive IRQ load that motivated D39. The command ID and save format remain unchanged. | Retain the historical raw reload byte: permits fine pitch changes but `S00` requests a 1 MHz interrupt and combining it with 4× source stepping can starve playback and rendering. Add the command as a multiplier on top of TSP: allows 16× source consumption and makes rate reasoning needlessly compound. Make S persistent across notes: useful automation state, but disagrees with ALYNXDJ's established next-trigger reset and makes the instrument RATE cease to be authoritative. |
| D48 | **KIT TSP uses a dense, hardware-safe rate sequence: `FF`=0.5× and `00`–`03`=1×–4×.** `02` now supplies the missing exact 3× stride and `03` becomes 4×. Six-piece frame/render refill service points sustain that range, while the IRQ performs a cheap proximity check and advances bytewise only when a stride could cross the currently published head; this makes odd strides and live rate changes stop safely instead of entering unpublished/stale ring data. A true `04`=5× state was implemented and measured, but its ~26 KB/s serial-cart demand repeatedly underruns the longest factory sample even with aggressive refill, so it is deliberately not exposed as a “sometimes” mode. Existing stored KIT `$02` patches now mean 3×; backward semantic compatibility is intentionally not retained while usage is negligible. `Sxx` keeps D47's separate four-state low-two-bit mapping. | Keep sparse `02`=4×: avoids reinterpretation but leaves an unintuitive hole at 3×. Expose tested 5× anyway: attractive on short samples, but reintroduces hardware-dependent jitter and timing degradation. Raise the cushion beyond six pieces: does not solve 5× steady cartridge bandwidth and worsens trigger latency. |
| D49 | **TAPS and SEED render at their exact three-digit widths.** The 9-bit TAPS field displays `000`–`1FF` beside its nine-bit block map, and the 12-bit SEED field displays `000`–`FFF`; neither carries a redundant leading zero. The compact renderer overlaps two existing byte draws. Cart-reader scratch, the VBlank guard/counter, and the draw X offset move from MAIN BSS into free APPZP so the display change retains the full 512-byte C stack and every song/audio buffer. APPZP is not cleared by crt0 on real hardware, so the VBlank guard is explicitly zeroed before its IRQ is enabled; the harness dirties that byte before startup to guard against emulator-zero-fill regressions. Stored instrument records and save format v6 are unchanged. | Keep four digits: byte-oriented rendering is simpler, but falsely implies a 16-bit range. Remove the hexadecimal fields entirely: the TAPS bitmap is immediate, but exact patch entry and SEED editing still require numeric values. |
| D50 | **`E`, `N`, `P`, and `R` have explicit live-effect lifetimes and directions.** `Exy` installs attack time x and decay time y, then restarts the live attack stage so its high nibble cannot be hidden by the note trigger that preceded it. `Nxx` publishes a low-eight-bit tap replacement for the current note while preserving live tap 11 and the underlying G/B automation value; the next note republishes that instrument/automation state. `Pxx` follows patch SWP's period direction—positive bends down and negative bends up. `Rxy` owns its interval clock until the next note or `K`, so a naturally completed short LFSR envelope is restarted when the interval arrives; KIT retains its independent sample-refire path. No stored IDs, records, or save bytes change. | Let E change only rates without re-entering attack: its attack nibble is inaudible when it follows same-row note trigger. Store N into `tap_cur`: turns a per-note timbre override into persistent track automation and overwrites G/B. Keep P opposite to SWP: two pitch-rate controls disagree on sign. Gate R by `env_phase`: short envelopes prevent their own scheduled retriggers. |
| D51 | **MIDI expressive control enters the existing tracker command executor.** Channel CC74 (`Bn 4A vv`) maps its 7-bit value across `N00`–`NFF`, so it is a note-local live LFSR-taps replacement with exactly D50's lifetime. Standard Pitch Bend (`En ll mm`) ignores the sub-resolution LSB, maps MSB centre 64 and endpoints to an absolute ±2-semitone target in 1/16-semitone steps, and sends only each target delta through `F`; the four targets survive note retriggers and clear on panic/mode change. Consequently both controls inherit the tracker commands' established type limits rather than creating a second sound path. The Pico already forwards complete channel messages, so no private framing or bridge mode is added. | Invent proprietary ComLynx opcodes for tracker commands: compact, but no longer ordinary MIDI and risks collisions with data bytes. Directly mutate Mikey registers: smaller at first, but bypasses shadows, note lifetimes, and future engine fixes. Route every CC to an arbitrary command: flexible but difficult to document and unsafe without a user mapping UI. Use full 14-bit bend: finer than the 1/16-semitone engine and costs resident state/code with no audible benefit. |
| D52 | **The Pico bridge accepts USB MIDI and opto-isolated serial MIDI through one normalized dispatcher.** A 31,250-baud UART0 receiver on GP13 expands running status, preserves interleaved real-time messages, and discards SysEx/System Common before the existing channel/clock filters; use one source at a time because there is deliberately no merger/arbitration. The Chipbridge target is the Waveshare RP2040-Zero pinout: ComLynx output `DATA1` on GP1, serial MIDI on GP13, and active-high ready LED on GP7; `DATA0`/GP0 is unused by ALYNXDJ. Its custom Lynx cable connects ring DATA to PCB ring/GP1 and sleeve to sleeve, with both tips physically disconnected and insulated because the Lynx tip is +5 V. | Parse serial MIDI on the Lynx: spends scarce resident code/RAM and still needs physical conversion. Forward running status directly: violates D19's full-status stream and makes recovery stateful on the Lynx. Merge USB and TRS sources: ambiguous transport and doubled clock without a user-facing policy. Use an ordinary TRS cable: connects Lynx +5 V to the PCB GP0 network and is forbidden. |
| D53 | **The EEPROM pack buffer and live MIDI/sync helper have explicit interrupt exclusion.** `save_pack()` and runtime `save_load()` mark the shared `$C100-$C8FC` window unavailable before touching it. VBlank still acknowledges timer 2 and advances `frames`, but skips the complete engine tick while that mark is nonzero; this prevents stopped FILES work from branching into transient packed-song bytes. The cart reload uses the same zero-page byte as its 64-chunk countdown, so the helper becomes callable only after both blocks 254–255 are present. The byte otherwise remains the ordinary VBlank re-entry guard, adding no RAM. | Disable all IRQs for the whole pack/EEPROM operation: simple, but stalls nested hardware services for a long cold path. Guard only `sync_poll()`: smaller in concept, but another engine helper could later enter the same overwritten window. Trust FILES being stopped: `engine_tick()` still polls sync while stopped, which caused DEMO to execute packed data. |
| D54 | **Palette `01` is the clean-machine default.** Boot initializes PALETTE to `01` after fixed-state clearing; a valid persisted OPTIONS config still replaces it with the user's stored `00`–`07` choice. No EEPROM or song format changes. | Keep `00` as the fallback: valid but no longer the preferred presentation. Swap palette definitions while displaying `00`: misrepresents the selected preset and changes saved-setting meaning. |
| D55 | **Legacy Lynx-to-Lynx `START` is also the downbeat/first row grant.** An OUT transport sends the single `$02` byte when SONG, CHAIN, PHRASE, contextual, or LIVE playback starts. IN preserves an already-cued local position, otherwise loads song row 0, and presents that row on the engine tick which consumes START; later rows remain one `$01` grant each. This removes the hardware-observed one-row startup offset without sending adjacent START+ROW bytes that could overrun Mikey's polled one-byte receiver. The corrected timing is verified as flawless between two Lynx II units; their independent VBlank phases can theoretically still contribute less than one engine tick (0–16.7 ms) of onset skew. | Keep START as transport-only and wait for OUT's first boundary ROW: puts IN one complete tracker row behind. Send START immediately followed by ROW: works with an IRQ/FIFO, but legacy IN is deliberately polled and Mikey has only a one-byte receive holding register. Interrupt-buffer all legacy sync: costs another live buffer or conflicts with phrase-pass state for traffic that is otherwise only one byte per row. |

Open questions: see §14 and PLAN.md — the two-voice software policy is
regression-tested; its final silicon timing margin and Handy's LFSR fidelity
still require focused hardware comparison.

---

## 1. Hardware constraints that drive the design

- **CPU:** 65C02 core in Mikey, ~4 MHz minus video DMA/refresh steals (~3.6 MHz
  effective). Comparable to the siblings' Z80 in throughput; poorer at 16-bit
  math — ROM lookup tables everywhere, no mul/div on hot paths (ported rule).
- **RAM: 64 KB unified, and everything lives in it** — code, song, framebuffer,
  stack. There is no runtime ROM: the cart is a block-addressed serial device
  read through `RCART`; most code is loaded by the standard loader, with three
  cold-code overlays copied from cart blocks 40, 42, and 44 into `$C900`,
  `$F600`, and `$F320` before use; the sample bank occupies blocks 45–249,
  HELP uses blocks 250–253, and the MIDI helper occupies the final blocks
  254–255. The
  1 KB at `$D000` is split into two 512-byte sample rings; the framebuffer
  remains at `$A000` and the 512-byte C stack below it.
- **Cart:** the current 256 KB image uses 256 × 1 KB blocks, read sequentially
  after an 8-bit block seek. A 512 KB image instead uses 256 × 2 KB blocks,
  requiring a deliberate remap of code overlays and sample offsets (D25).
  Streaming sample PCM is a good fit (D5); random-access live state stays in RAM.
- **Video:** 160×102, 4 bpp packed framebuffer (80 bytes/line), 16 pens from a
  4096-colour palette, ~59.9 Hz with the current crt0 timing, driven by timer 0 (HBlank) → timer 2
  (VBlank, linked). No tiles, no tilemap — pure framebuffer (D7/D9).
- **Sound (Mikey):** 4 identical channels at `$FD20+8n`, each: `VOLUME` (8-bit
  signed amplitude), `FEEDBACK` (LFSR tap enables), `OUTPUT` (8-bit signed DAC
  value, directly writable), `SHIFT`/`OTHER` (12-bit LFSR state), `BACKUP`
  (timer reload), `CONTROL` (clock prescale 1–64 µs + link, count enable,
  integrate bit, tap 7). Each channel is a down-counter clocking a 12-bit
  polynomial shift register:
  - taps → alternating sequence = **square wave** at `clock/(BACKUP+1)/2`;
  - other tap sets = **pitched polynomial noise**, the Lynx's signature
    metallic/gritty timbres — a *timbre family*, not one noise;
  - **integrate mode** accumulates ±VOLUME per underflow = triangle/saw ramps;
  - timer off + CPU writes to `OUTPUT` = **8-bit signed PCM DAC**, per channel.
- **Stereo (Lynx II only):** `ATTEN_A–D` ($FD40–43, 4-bit L/R nibbles),
  `PAN` ($FD44), `MSTEREO` ($FD50). Zero level also uses MSTEREO's hard
  channel-side gate (D44). Mono mix on Lynx I (D8).
- **Timers:** 8 system timers. 0 = HBlank, 2 = VBlank (display pair), 4 = UART
  baud (ComLynx). 1/3/5/6/7 free — PCM feed IRQs and the EEPROM bit-bang
  delay live here.
- **ComLynx:** open-collector UART (`SERCTL`/`SERDAT`), multi-drop up to ~16
  units — the sync port (§11). Byte-oriented, unlike the siblings' 2-bit
  DE-9 counter.
- **Persistence:** 93C46/66/86 serial EEPROM (128 B / 512 B / **2 KB max**),
  bit-banged via `IODAT`/`AUDIN`; declared in the `.lnx` header (byte 60) and
  emulated by Handy. The entire save design flows from this constraint (D4).
- **Input:** D-pad + **A** + **B** + **Option 1** + **Option 2** + **Pause** —
  one more pair of buttons than the SMS pad, one fewer than the MD 3-button.
- **No region split.** One machine, one clock, one set of tables (D3).

## 2. Song data model *(ported — SMSGGDJ §2 verbatim in shape)*

```
SONG → CHAIN → PHRASE → notes + INSTRUMENT refs + commands
                         INSTRUMENT → optional TABLE
GROOVE tables control tick timing globally
```

Four tracks **CH1–CH4** (D1). Structure sizes (RAM tier — save packing in §12):

| Structure | Per-unit | Count | Bytes |
|---|---|---|---|
| Song rows | 4 tracks × chain # (1 B) | 128 rows | 512 |
| Chains | 16 × (phrase #, transpose) | 32 | 1024 |
| Phrases | 16 × (note, instr, cmd, param) | 64 | 4096 |
| Instruments | 16 B fixed record | 32 | 512 |
| Tables | 16 × (vol, pitch, cmd, param) | 16 | 1024 |
| Grooves | 16 ticks | 16 | 256 |
| Waves | 32-byte 8-bit wavetable | 8 | 256 |
| **Total** | | | **7680 B** |

Empty slots are `$FF` sentinels (ported). "Blocks free" counter on PROJECT.
The block is contiguous and offset-stable — SAVEFORMAT.md will fix the map.

## 3. Control input *(ported control philosophy, remapped to the Lynx pad)*

The settled frame: **the button already held when another arrives selects the
action; no simultaneous-press timing windows** (the only windows are
sequential double-taps of B). Mapping uses the physical button labels:

| Lynx | Role (sibling equivalent) |
|---|---|
| **B** | item-level modifier — SMSGGDJ button 1: tap = insert/activate (on stopped INSTR: audition current patch), hold + D-pad = edit value, double-tap = paste/mint/slim-clone. Empty SONG/CHAIN cells mint the lowest blank and unreferenced chain/phrase; occupied cells copy only that chain/phrase, retaining its nested references. |
| **A** | project-level modifier — SMSGGDJ button 2: hold + D-pad = screen-map nav, held + B = play/stop transport. A clean tap backs out of some detail/utility screens but is inert on CHAIN, PHRASE, INSTR, OPTIONS, and PROJECT, where map navigation is authoritative. |
| **B held + A** | cut; + long-hold = block SELECT (ported gesture set). A PHRASE command-column cut carries only CMD+PARAM and preserves NOTE+INSTR. |
| **Option 1** | clean tap = stop active/WAIT transport, otherwise start the contextual all-track arrangement (D28); held + D-pad L/R selects track, held+B = mute toggle, held+A = solo |
| **Option 2** | LIVE-mode page toggle on SONG |
| **Pause** | play/stop alias; double-press = panic (silence, abort PCM, re-arm sync) — ported NMI semantics, but on the Lynx Pause is a normal pad line read per-frame |

Key-repeat (DAS-style) ported from `input.asm`.

## 4. GUI layout and screens *(ported 2D screen map, new geometry)*

40×17 character grid (D7). Persistent chrome: top bar = screen name, song
title, BPM, play state, sync state, position `SS:CC:PP`; the screen-map
indicator occupies the upper-right area. Channel activity meters were removed
at D26 to protect sample and editor timing. Screen bodies are offset eight
character columns to the right of their original positions, except WAVE and
HELP, which retain the full width; the top bar and screen map stay fixed.

Screen map (physical A-held + D-pad), same 2D shape as SMSGGDJ — OPTIONS above
SONG, PROJECT above CHAIN, **WAVE above INSTR**, **HELP above TABLE**,
**FILES below SONG**, GROOVE below CHAIN:

```
OPTIONS  PROJECT             WAVE    HELP
   |        |                  |       |
SONG  →  CHAIN  →  PHRASE  →  INSTR → TABLE
   |        |
FILES    GROOVE
LIVE is a SONG-screen mode toggle (M12).
```

Horizontal drill-in follows the object under the cursor. SONG → CHAIN and
CHAIN → PHRASE select that object and place its child cursor on row `00`.
PHRASE → INSTR selects the valid instrument assigned to the current phrase
row; an empty row or invalid instrument keeps the previously viewed patch.

The right column carries the **map indicator** (ported): three rows
(`OP_WH` / `SCPIT` / `FG___`), current screen inverted. HELP hides it only
after entry so its 38-column text can use the full display width.

HELP follows the sibling trackers' source-data contract rather than embedding
prose in code. `tools/makehelp.py` converts editable `help.txt` into a compact
`AHD1` binary, validates 38 columns × 16 rows, page count, uppercase font
coverage, and its three-block cart budget, and bakes the current version/build
stamp into the final page. The ordering is navigation, cell/block editing,
mint/clone and structure, instruments, WAV/KIT/tables, commands A–L, commands
N–Z, playback/sync, then FILES/limits/version.

Inverse-video cursor, playhead row highlight, and full-data-row block selection
— ported. The
16-step phrase grid + chrome fits 17 rows exactly; no paging (the GG build's
paged-WAVE compromise is not needed at 40 columns).

## 5. Sound engine

### 5.1 Timing
Engine tick = VBlank IRQ at ~59.9 Hz (D3, measured at M1). The groove model
is ported unchanged: a groove is up to 16 tick-counts, tempo *is* the groove,
PROJECT TMPO walks the NTSC-60 BPM rungs, `T` does BPM→groove conversion
with 60 Hz constants. In sync-slave mode row timing is clock-driven and
grooves are ignored (ported semantics, §11).

### 5.2 Per-tick pipeline *(ported from engine.asm, one voice type richer)*
groove → row advance → trigger/command peek (`D`/`L`/`I`/`Z`/`J` pre-trigger,
ported) → command execute (one shared executor for phrase + table columns) →
AHD envelope → kill → **channel shadow compose** → **flush**. The shadow set
is Mikey-shaped: per channel `{BACKUP, CONTROL, FEEDBACK, VOLUME, ATTEN}` +
dirty bits; flush writes only what changed (ported `psg_flush` discipline).
No SCB/BUSREQ split — one CPU owns everything; the flush is a plain
subroutine, the *engine order* is what ports.

### 5.3 Voice model — where ALYNXDJ diverges
Every channel runs one of three voice types, per-instrument (D1/D35):
- **LFSR** — the full Mikey polynomial oscillator, from square and short
  metallic cycles through long noise-like cycles. Pitch = prescale +
  BACKUP from the note table; software AHD envelope into the 7-bit volume
  magnitude (the 4→7-bit upgrade over the SMS makes envelopes *smooth*).
  Per-instrument SWP/VIB shape pitch; TRM applies a descending 6-bit saw to
  the live AHD level, snapping back to the peak each cycle so it doubles as a
  lightweight repeating decay envelope (D15);
  VIB is a centred, note-continuous sine at ~0.47–7.49 Hz with SMSGGDJ's
  nonlinear depth response expressed in key-independent semitones (D16).
  It exposes the full 9-bit TAPS mask and 12-bit SEED (D11).
- **WAV** — two sub-modes: *integrate* (hardware tri/saw ramps, free) and
  *table* (32-byte 8-bit wavetable looped through the channel DAC by a timer
  IRQ — PCM-cost, counts against the D6 cap). Table playback targets a ~6.25
  kHz ceiling by stepping through 2/4/8 entries at higher notes; pitch is
  preserved while waveform resolution falls to protect the real-hardware
  dual-DAC VBlank budget. Extreme high notes may exceed the target once the
  8-entry step is exhausted rather than being silently pitch-clamped.
- **KIT** — 8-slot sample kits streamed from cart through the channel DAC at
  D39's fixed timer-IRQ rate (D5). Instrument TSP is RATE: it repeats or
  skips source bytes for 0.5× and exact 1×–4× without increasing interrupt load
  (D46). `S` temporarily replaces that source stride with one of the same
  four bounded rates while leaving the timer fixed (D47).

### 5.4 Playback modes *(ported)*
Physical A+B retains selected-track song / chain-loop / phrase-loop preview.
A clean Option-1 tap stops an active or waiting transport; while stopped it
starts arrangement playback on all tracks from the selected screen context
(D28). In SONG playback, every track treats empty
cells as group delimiters and loops only the contiguous non-empty run from
which it was started (D29). LIVE remains a quantized clip launcher; pending
starts show their chain and pending stops show `ST`, both inverted-accent.

## 6. Instruments

16-byte fixed record, union by type (ported shape): common = type, initial
volume, AHD (attack/hold/decay), table #, pan nibbles, finetune, raw TAPS and
SEED. HOLD's high nibble is **TBS** (D17); low-nibble values 0–E are timed
peak holds and F sustains indefinitely (D22). Bytes 12–14 are LFSR
**SWP** (signed 1/16 semitone per tick,
period-style direction: positive falls),
**VIB** (speed/depth nibbles), and **TRM** (speed/depth nibbles); phrase/table
`P` and `V` commands override the corresponding instrument state for that
note. Byte 15 is signed **TSP** in semitones for LFSR/WAV (D16), resolved
before pitch and clamped to the playable note range. KIT reuses it as
source-step **RATE** (`FF` 0.5×, `00`–`03` 1×–4×; D48), and its
pad follows only the already phrase/chain-transposed note. WAV uses
WAVE `$FF` for hardware integrate or 0–7 for a table; KIT uses the same byte
as kit bank 0–7 while the transposed note semitone selects its member. KIT
has no empty bank state: selecting KIT or viewing a historical invalid value
normalizes the byte to 0, while WAV retains `$FF`.
After NEW, all instruments default to LFSR, VOL `$7F`, ATK `0`, HOLD `5`,
DCY `5`, and TAPS `$001`.

INSTR begins with an editable instrument-number field. Left/Right select
±1 and Up/Down select ±16 with wrapping across `00`–`1F`; this changes only
the patch being viewed and edited. Physical B while stopped directly auditions
the selected instrument at the last-entered note without changing transport.
Physical A-held+B keeps the contextual phrase-loop transport, so patch preview
and musical-context preview remain distinct gestures. The remaining rows are
type-aware: LFSR shows the oscillator, modulation, envelope and table
surface; WAV shows its envelope, wave and table surface; KIT shows VOL, TSP
and its sample-kit selector. Hidden fields retain fixed row positions but are
also skipped by the cursor.

## 7. Tables *(ported verbatim)*
16-row macro sequencer: vol, pitch(transpose), cmd, param columns; PHRASE `A`
supplies a per-note table override, while TABLE offers only
`B C E F G H K N O P R S T V W X`; `H` loop semantics are identical to
SMSGGDJ §7.
Deleting from the command-letter column clears only CMD+PARAM.
TBS 0 advances once per note and preserves the row, including repeated
PHRASE `Axx` selection of the same table; changing the A target restarts it.
TBS 1–F advance every N ticks and restart on note-on. Every mode wraps
`0F→00`, while `H` defines a shorter or non-zero loop point (D17). VOL can
set the live level/peak during attack or hold, but never writes during decay,
so a looping table cannot extend the envelope lifetime.

## 8. Command set *(ported, with Lynx re-aims)*

The implemented set is `A B C D E F G H I J K L N O P R S T V W X Z`.
PHRASE steps through that order in both directions. TABLE skips
`A D I J L Z`, so its valid subset is `B C E F G H K N O P R S T V W X`.
The empty command wraps between each context's endpoints and stored command
IDs remain stable. Editing either byte remembers the complete command/value
pair globally; a clean physical-B tap on an empty command-letter cell recalls
the pair only when it is valid in that context (D45).
The Lynx-specific or re-aimed commands are:

| Cmd | Lynx meaning |
|---|---|
| `G xx` | signed direction + split tick/row period for one-step motion of the raw 9-bit tap value, without reseeding |
| `B xx` | cumulative signed addition to the current raw tap value; `B00` restores the active instrument and releases the accumulator |
| `E xy` | install attack x/decay y and restart the live attack stage |
| `N xx` | current-note low-eight raw-tap override (taps 0–5, 7, 10); preserves tap 11 plus underlying G/B state |
| `O xy` | pan: left/right level nibbles (`0` hard-mutes that side, `F` full), 16 levels per side where supported; next note restores instrument PAN (D8/D42/D44) |
| `P xx` | signed pitch rate in SWP direction: positive bends down, negative bends up |
| `R xy` | retrigger every y ticks with −8x peak fade; its LFSR interval survives natural envelope end |
| `S xx` | KIT source-rate override; low two bits select 1×/2×/4×/0.5×, and the next note or `R` restores instrument RATE (D47) |

## 9. Groove *(ported timing model; one global groove, 59.9 Hz constants)*

## 10. Samples

- **Tool/format:** `tools/alynxdj_pool.py`: 8/16/24/32-bit integer WAV →
  5,208.333 Hz 8-bit signed PCM, peak-normalized then mastered through
  +12.00 dB of tanh drive, 8 slots/kit, self-describing `PL` directory.
  Header byte 3 is the canonical rate ID `1`; other IDs are rejected. The tracked
  `samples/alynxdj-factory-samples.bin` is the canonical factory bank and is
  injected verbatim into every default build. `make factory-samples` rebuilds
  it from the repo's WAV kits; `make SAMPLE_BANK=/path/custom.bin` validates
  and injects another bank. `sample-patch-browser.html` imports and exports
  exactly this binary format so banks move unchanged between ROM releases.
  It always presents KIT `00`–`07`; if an imported ROM/bank declares fewer
  kits, the remainder are editable empty pads represented by one silent byte
  each when exported. Filling them is permitted whenever the shared pool cap
  still fits.
  Its ordinary one-shot importer retains the normalized conversion path; its
  direct RIFF/WAVE parser and band-limited converter pin source rate, duration,
  and pitch before the Slice-to-kit workflow keeps one long source
  unnormalized, applies shared gain/tanh, and optionally fades each equal
  slice before pad mapping.
  The main-loop trigger reads only the selected five-byte directory entry;
  the full 320-byte directory is not held in RAM.
- **Capacity:** blocks 45–249 provide 209,920 bytes. The directory's u16 length
  allows up to 65,535 bytes (~12.58 seconds) for any one sample, but all 64
  samples share the bank cap. More slots are deliberately not added (D25).
- **Playback:** timer 7/5 feed two dynamic DAC slots, each targeting the
  owning track's `OUTPUT` register. Two independent 512-byte rings share the
  1 KB at `$D000`. A lone stream retains the cartridge's sequential cursor;
  alternating voices re-seek when ownership changes, and directory reads
  explicitly invalidate the cursor owner. The two normal pump calls can
  refill up to five 64-byte pieces each per display frame, continue onto the
  wrapped half of the ring, and publish each piece immediately. The IRQ-owned
  tail is snapshotted atomically, and each piece's
  16-bit head plus done flag is published under one brief IRQ mask. The editor
  pumps after playhead redraws and cooperatively every four rendered glyphs;
  the large grid clear is divided into sixteen bands with a pump between them.
  Refill reads stay at 64-byte granularity rather than degenerating into tiny
  cart transactions at those frequent service points. Same-track KIT
  retriggers keep the prior sample sounding until the replacement has its
  startup ring ready. Together these rules remove torn-pointer overwrites,
  ring-end bandwidth loss, redraw starvation, and held-DAC gaps.
  Ring underrun holds the last sample until refill.
- **Budget:** at 5.208 kHz one PCM voice ≈ 5208 × ~45 cycles ≈ 6 % CPU; two ≈
  12 % (D6, with final silicon margin still tracked by Q2).
- **No 4-bit log mapping, no DAC arbitration with tone duties** — each PCM
  voice owns a whole channel. The SMSGGDJ §10 machinery this replaces stays
  behind as documentation only.

## 11. Sync — ComLynx *(new transport, ported semantics)*

`SERCTL`/`SERDAT` UART, open-collector multi-drop. Implemented OPTIONS modes:
**OFF / OUT / IN / MIDI / IN24**.
- **OUT:** master sends one byte per row (row-clock, the siblings' settled
  1-clock-per-row model) + transport bytes. START is itself the downbeat grant,
  while each later row uses ROW and STOP halts.
- **IN:** slave row-advances per received row byte; grooves/`W` ignored
  (ported slave semantics). Local transport arms at the selected row and
  displays `WAIT`; START presents that row and changes to `PLAY` (or selects
  song row 0 when no local cue exists), and subsequent ROW bytes advance.
- **MIDI:** exclusive receive-only takeover. The transport stays stopped while
  the 59.9 Hz voice tick continues envelopes, tables, and modulation. A bridge
  converts USB or serial MIDI to ComLynx's 62.5-kbaud, 8-data + fixed-Space-ninth-bit
  framing and emits complete (non-running-status) messages. MIDI channels 1–4
  drive tracks A–D with instruments 00–03; MIDI notes 24–119 map to the
  tracker's C1–B8 range. Note On velocity zero is Note Off; other velocities
  are not yet applied. Note Off releases through DECAY, CC120/123 release the
  channel, and System Reset `$FF` is a hard panic. CC74 uses the same live
  `N` path as a phrase/table command; Pitch Bend uses the same additive `F`
  path, with an absolute ±2-semitone target remembered per channel across
  notes. There is no heartbeat or timeout in this version. The UART IRQ
  captures bytes continuously; the
  engine tick drains and applies them before ordinary voice processing, so a
  redraw cannot defer MIDI and serial reception remains enabled throughout a
  multi-channel trigger batch. Trigger latency is quantized to the next
  59.9 Hz tick (0–16.7 ms).
- **IN24:** row-rate clock via the Pico bridge. The Pico counts standard MIDI
  Clock from the selected USB or serial source. `FA`/`FB` is followed immediately by the first `F8` row grant;
  every six later source clocks emit the following grant, giving four tracker
  rows per quarter note without a one-row startup wait. Local transport has
  the same cued `WAIT` behavior as `IN`. `FA` Start arms song row 0 only if no
  local cue is waiting; `FB` currently does the same because there is no Song
  Position Pointer state; `FC` Stop halts. Each received `F8` grants one row.
  Stored groove and `W` timing are ignored just like ordinary slave `IN`.
- MIDI bytes are IRQ-buffered because Mikey's one-byte UART holding register
  cannot retain a serialized chord while a trigger masks receive. The engine
  tick drains the 64-byte ring with nested UART IRQs enabled; the ring reuses
  `$C048-$C087` while the sequencer is stopped.
  `IN24` needs the phrase counters while playing and therefore uses a separate
  64-byte IRQ ring over the otherwise-idle phrase clipboard at `$C088-$C0C7`.
  Entering/using `IN24` therefore invalidates that clipboard. The parser,
  sync, contextual-play, and editor cold-code overlay occupies `$C100-$C8FC`
  between pack operations. The VBlank engine tick is gated for that complete
  lifetime, and FILES reloads both cart blocks 254–255 before releasing the
  gate (D53).
- `pico-midi-comlynx/` is the reference bridge. It enumerates as a USB-MIDI
  device for a computer/DAW and also accepts opto-isolated 31,250-baud serial
  MIDI on GP13. Serial running status is expanded and both sources share one
  normalized dispatcher. It forwards channel 1–4 messages, emits one `F8`
  immediately after `FA`/`FB` and then per six source clocks, forwards
  `FC/FF`, and emits start + 8 LSB-first data + fixed-Space ninth + stop at
  62.5 kbaud using PIO. The Chipbridge build targets an RP2040-Zero with
  ComLynx GP1 and ready LED GP7. It is send-only, has no heartbeat, does not
  merge simultaneous MIDI sources, and does not host a USB controller.
- Multi-drop means one master can drive up to ~15 slaves — a Lynx orchestra
  is a headline feature; keep the wire protocol dumb (single-byte opcodes,
  no addressing) so it stays achievable.
- PULSE (analog Volca-style) does not exist here — ComLynx is digital-only.
  Cross-family (DE-9 ↔ ComLynx) sync happens through the ESP32 bridge, not
  by wire.

## 12. Persistence *(the Lynx-unique problem — D4, D10)*

- Working song: full-size flat block in RAM, RAM-only until explicit SAVE
  (GENMDDJ's settled persistence model).
- SAVE: RLE-pack the 7680-byte flat block into the 2032-byte song payload of
  the 93C86, with an 8-byte/four-word header (magic `ALDJ`, version, packed
  length, checksum). FILES shows a live packed-size meter (D10);
  SAVE refuses (with the meter red) rather than truncates.
- Current format is v6: instrument HOLD's high nibble is TBS, bytes 12–14
  are SWP/VIB/TRM, and byte 15 is LFSR/WAV TSP or KIT RATE. Older writers always stored HOLD in
  0–15, so v5 and earlier naturally load as TBS 0 (D15–D17).
- LOAD: unpack EEPROM → RAM at boot if the magic checks (slot-0 autoload,
  ported policy). If validation fails or EEPROM is empty, keep the clean NEW
  song; the factory demo is available explicitly from FILES.
- Hardware writes use cc65's canonical 93C86 EWEN bit pattern (all ten
  special-command address bits high), because SD-cart EEPROM emulators may
  decode it more strictly than the physical chip's don't-care definition.
- Emulator persistence is the core's 2048-byte `.eeprom` image. The standalone
  `song-file-viewer.html` browser tool imports that same image (including the
  names SRAM/SAV/E2P used by SD carts), validates and edits the complete song
  model, and exports a checksum-correct v6 image without uploading it. ComLynx
  song dump/restore remains post-1.0 work.
- SAVEFORMAT.md is written at M10 and kept in sync with any RAM-map change
  (standing sibling rule).

## 13. Frame budget (65C02, ~3.6 MHz effective, 59.9 Hz tick ≈ 60 K cycles)

| Consumer | Budget |
|---|---|
| Engine tick (4 voices, pipeline §5.2) | ≤ 6 K cycles |
| Flush (shadow → Mikey) | ≤ 1 K |
| PCM IRQs (2 × 5.208 kHz, D6/D39) | ~7 K amortized/frame |
| Text render (dirty cells only) | ≤ 8 K typical |
| Input + editor logic (cc65 C) | the remainder (~20 K) |

Rules (ported): no mul/div on hot paths; note/curve/BPM tables live in RAM;
the PCM IRQ handler does not touch editor state; VBlank runs the engine while
the main loop handles input, cart-ring refill, and rendering.

## 14. Open questions

| Q | Question | Blocks | Path |
|---|---|---|---|
| Q1 | ✅ **RESOLVED at M10b** — the current v6 factory song + out-of-loop rigs RLE-packs to **1265/2032 bytes**. `make test` power-cycles a unique Handy EEPROM and verifies the song checksum. | M10 | — |
| Q2 | 🔶 **Two-voice routing is correct and the 5.208 kHz silicon performance improvement is confirmed; final stress margin remains open:** hardware held tempo with one DAC voice but slowed on demo rows 04–05 where a 7–10.5 kHz table-WAV overlapped the former 7.8 kHz KIT. The WAV ceiling is now ~6.25 kHz (higher notes skip table points without changing pitch), and D39's 5.208 kHz canonical KIT rate has materially improved real-hardware performance. Kit-00 F4's first discontinuities were a cart-seek borrow bug replaying data at 1 KB page crossings; a second capture exposed genuine ring underruns from refill publication around ring wrap and redraw work. The stream now retains its cart cursor, publishes efficient 64-byte pieces across the wrap, and D27 gives it bounded service points throughout every screen redraw and grid clear. Same-track retriggers keep the prior sample live until the replacement buffer is ready. D26 additionally removes all channel-meter rendering and per-sample DAC peak accounting from the release ROM. Internal slot-underrun counters saturate, so zero unambiguously means clean. | M7/M22/M23/M34 | Recheck normal-ROM rows 04–05 plus repeated INSTR↔WAVE screen changes during two sustained/retriggered KIT voices on silicon at 5.208 kHz; both `$C027/$C028` counters must remain `00` |
| Q3 | ✅ **RESOLVED at M1 — 59.90 Hz** (crt0 timing kept; 96 VBL ticks per 120 Handy frames, i.e. Handy paces 75 fps but the emulated timer 2 runs 159 µs × 105 lines). maketables.py uses 59.90 Hz | M3 | — |
| Q4 | Handy's LFSR/integrate fidelity vs real Mikey | M6 | Curate tap presets on hardware; Holani core as a second opinion |
| Q5 | ✅ **RESOLVED — persistence depends on the cart's EEPROM capacity, and the RetroHQ Lynx GameDrive is hardware-verified.** The BennVenn ElCheapoSD contains a physical 128-byte 93C46 and supports only that EEPROM type; ALYNXDJ requires a 2 KB 93C86 and currently packs the demo to 1265 bytes. The supplied 128-byte `.sav` has no `ALDJ` header and contains FAT directory entries. The ElCheapo's separate API is menu-loader-oriented, not general filesystem access, so neither protocol timing nor a ROM-side SD fallback can preserve a full song. The release ROM's `.lnx` byte 60 = 5 and the patched core correctly implement 93C86. On real Lynx hardware, the GameDrive successfully saves, reloads, and retains ALYNXDJ songs when presented with the complete EEPROM backing image. | M10/M17/M54 | Use the verified GameDrive, another 93C86-capable/emulating cart, or Handy for persistent songs; future ComLynx transfer can provide off-cart backup but does not make ElCheapo persistence automatic |

## 15. Deliverables & toolchain

`make` → `build/alynxdj.lnx` (cc65: `cl65 -t lynx`, project-local cfg as the
cart layout grows); `make shot` → headless Handy screenshot + audio WAV
(the audio capture is the FFT-verification path for every sound milestone,
ported practice from GENMDDJ). Python tools: `makefont.py` (4×6 font),
`maketables.py` (note/BPM tables for 59.9 Hz), `alynxdj_pool.py` (pool).
Build stamp: git hash on the boot splash (ported; catches stale flashes).
Truth on real hardware via an SD cart; Handy is dev-speed, not silicon.
