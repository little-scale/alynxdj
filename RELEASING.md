# Releasing ALYNXDJ

House etiquette for cutting a release. The guiding rule: **the git tag must
point at the exact commit the shipped binary was built from.** Everything
below exists to keep that true.

## Golden rules

1. **Freeze first, tag last.** Land *every* change for the release on `main`
   before you create the tag. Don't tag mid-stream — if one more tweak comes
   in after tagging, the tag no longer matches the binary and you're forced
   into a tag move (a force-push).
2. **Build the release ROM from the final commit.** The boot splash carries
   the git build stamp (`build/buildid.h`). Commit, *then* `make clean &&
   make dist`, so the stamp is the clean release hash with no trailing `+`
   (the `+` means the tree was dirty at build time — never ship that).
3. **One version, three places.** Bump `VERSION` in the `Makefile`, add the
   dated section to `CHANGELOG.md`, and update the Status line in
   `README.md`. `VERSION` flows into `build/buildid.h` and the splash.
4. **Tag the build commit; don't push the tag early.** Push `main`, verify,
   then tag and push the tag as the last step before `gh release create`.
5. **Moving a pushed tag is a force-push.** It rewrites the remote ref — only
   do it with explicit sign-off, and only when the tag is fresh with no
   release or consumers yet.

## Steps

1. **Land all changes** on `main` and confirm `git status` is clean.
2. **Bump the version:**
   - `Makefile`: `VERSION := vX.Y`
   - `CHANGELOG.md`: turn the `## Unreleased` section into `## vX.Y — YYYY-MM-DD`
     with a one-line summary; keep the bullets tight and user-facing.
   - `README.md`: update the **Status** line.
3. **Commit** the version bump (e.g. `Release vX.Y: changelog, version, README`)
   and `git push`.
4. **Build the assets from the just-pushed commit:**
   ```sh
   make clean && make dist          # -> build/alynxdj_vX_Y.lnx (clean stamp)
   ```
   Confirm `build/buildid.h` shows the release hash with **no** `+`.
5. **Render the demo WAV** and trim the boot/splash lead-in:
   ```sh
   rm -f build/*.eeprom             # so the demo plays, not a stale save
   tools/emu/retroshot tools/emu/handy_libretro.dylib build/alynxdj_vX_Y.lnx \
       build/demo.ppm 0 "0@280,100@3,101@3,100@2,0@760"
   # then trim leading silence to ~0.2 s -> build/alynxdj-vX.Y-demo.wav
   ```
   (Playback starts with the post-A/B-swap transport gesture: physical A held,
   then B — mask `100@n,101@n,100@n`. The song audio is identical across
   doc-only commits, so it doesn't need re-rendering for a re-tag.)
6. **Stage the release assets** (naming convention):
   - `alynxdj_vX_Y.lnx` — the ROM (`make dist` output)
   - `handy_libretro-macos-arm64-alynxdj.dylib` — the patched verification core
     (`cp tools/emu/handy_libretro.dylib build/handy_libretro-macos-arm64-alynxdj.dylib`)
   - `alynxdj-vX.Y-demo.wav` — the trimmed demo render
7. **Tag the build commit and push the tag:**
   ```sh
   git tag -a vX.Y -m "ALYNXDJ vX.Y — <one-line theme>"
   git push origin vX.Y
   ```
8. **Cut the GitHub release:**
   ```sh
   gh release create vX.Y \
       build/alynxdj_vX_Y.lnx \
       build/handy_libretro-macos-arm64-alynxdj.dylib \
       build/alynxdj-vX.Y-demo.wav \
       --title "ALYNXDJ vX.Y — <theme>" --notes-file <notes.md>
   ```
   Notes should lead with the theme, list highlights, and carry the **emulator
   caveat** where relevant (this Handy build doesn't emulate the LFSR feedback,
   so TAPS/`N`/`G` are register-level + hardware verified only).
9. **Verify:** `gh release view vX.Y` shows the three assets and the tag points
   at the build commit. Update the project memory with the release status.

## Notes

- `build/` is gitignored — assets are attached to the release, not committed.
- The core `.dylib` is the repo-built patched libretro-Handy (see CLAUDE.md);
  ship the macOS-arm64 build until other platforms are built.
- Seb has no non-macOS build pipeline yet, and no cross-platform cores — the
  release ships the ROM (portable) plus the macOS core as a convenience.
- If a change *must* go in after the tag is pushed: land it, then move the tag
  to the new commit (`git tag -d`, re-tag, `git push --force origin vX.Y`) —
  with sign-off — and rebuild the assets from that commit before releasing.
