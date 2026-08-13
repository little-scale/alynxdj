# Releasing ALYNXDJ

House etiquette for cutting a release. The guiding rule: **the git tag must
point at the exact commit from which the shipped ROM was built.**

## Golden rules

1. **Freeze first, tag last.** Land every release change before creating the
   tag. Moving a published tag rewrites history and requires explicit sign-off.
2. **Build from a clean release commit.** The boot splash includes the git
   hash from `build/buildid.h`. A trailing `+` means the tracked tree was dirty
   when the ROM was built and that ROM must not ship.
3. **Keep the version aligned.** Update `VERSION` in `Makefile`, add the dated
   `CHANGELOG.md` section, and update the README Status line.
4. **Run the complete suite twice where it matters.** Test the proposed source,
   then build and test the exact clean commit that will receive the tag.
5. **Ship the complete six-file bundle.** The ROM, portable factory samples,
   both standalone browser tools, standard Pico bridge firmware, and
   RP2040-Zero Chipbridge firmware are the supported release set.

## Steps

1. **Prepare the release metadata:**
   - `Makefile`: `VERSION := vX.Y`
   - `CHANGELOG.md`: keep `## Unreleased`, then add
     `## vX.Y — YYYY-MM-DD` and concise user-facing bullets.
   - `README.md`: update the **Status** line and download description.
2. **Validate the proposed source:**
   ```sh
   make clean
   make test
   git diff --check
   ```
   Use `PYTHON=/path/to/python` and `NODE=/path/to/node` when the default
   runtimes do not provide the required packages.
3. **Commit and push the release source.** Stage tracked files explicitly so
   unrelated working files are not swept into the release.
4. **Build from the clean release commit:**
   ```sh
   make clean
   make dist
   make test
   ```
   Confirm `build/buildid.h` contains the release commit hash with no `+`.
   `make dist` writes `build/alynxdj_vX_Y.lnx`.
5. **Prepare the six assets:**
   - `build/alynxdj_vX_Y.lnx`
   - `samples/alynxdj-factory-samples.bin`
   - `sample-patch-browser.html`
   - `song-file-viewer.html`
   - `build/alynxdj_midi_comlynx.uf2`
   - `build/alynxdj_midi_comlynx_chipbridge.uf2`

   Build both UF2s with `make pico pico-chipbridge` when their source changed.
   If the Pico source is byte-for-byte unchanged and the SDK/toolchain is
   unavailable, a previous published UF2 may be reused only after confirming
   the source diff is empty and its SHA-256 matches the published asset.
6. **Record SHA-256 digests** for all six local assets.
7. **Create and push the annotated tag:**
   ```sh
   git tag -a vX.Y -m "ALYNXDJ vX.Y — <one-line theme>"
   git push origin main
   git push origin vX.Y
   ```
8. **Publish the GitHub release:**
   ```sh
   gh release create vX.Y \
       build/alynxdj_vX_Y.lnx \
       samples/alynxdj-factory-samples.bin \
       sample-patch-browser.html \
       song-file-viewer.html \
       build/alynxdj_midi_comlynx.uf2 \
       build/alynxdj_midi_comlynx_chipbridge.uf2 \
       --verify-tag \
       --title "ALYNXDJ vX.Y — <theme>" \
       --notes-file build/release-vX.Y.md
   ```
   Release notes should lead with the theme, summarize the main behavior
   changes, list all six downloads, state the save-format version, and retain
   the 2 KB 93C86 persistence caveat. Name the RetroHQ Lynx GameDrive as
   hardware-verified and the 128-byte ElCheapoSD as save-incompatible.
9. **Verify the public release:** confirm it is neither draft nor prerelease,
   contains all six assets, every remote digest matches the local digest, and
   the tag resolves to the release commit.

## Optional diagnostic assets

The patched macOS-arm64 Handy core and a rendered demo WAV may be attached
when useful for a timing or emulator-focused release, but they are not part of
the required portable bundle. Handy does not emulate Mikey's LFSR feedback, so
TAPS, `N`, and `G` claims must remain register-level plus real-hardware
verified.

## Recovery

- `build/` is gitignored; release assets are uploaded, not committed.
- If a fix is needed after a tag is pushed but before a release exists, land
  the fix and request explicit approval before moving the tag.
- If a public release already exists, prefer a new incremented version. Do not
  silently replace its tag or binaries.
