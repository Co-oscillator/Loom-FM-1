# Loom FM-1 Roadmap

This is the working roadmap for the Loom FM-1 firmware. It records the current
stabilization state, the release gate, and the next format change (FUN11).
Phase 2 product scope is intentionally left open until it is defined.

## Current state

- Device firmware: **v1.8.3**
- Repository: `main` at the stabilization commits after `8cb6d03`
- Host tests:
  - `tests/project_test.c`: **passing**
  - `tests/regress.c`: **passing** (goldens, health, voices/routing, CPU budget)
  - `tests/ui_test.c`: **20 visual/animation checks remaining**, all other groups green
- The firmware changes committed after v1.8.3 are **not built or flashed** yet.

## 1. Finish the visual/test pass (release gate for v1.8.4)

The remaining `ui_test` failures are all drawing/animation expectations tied to
pages the port moved or restyled. They are not functional regressions.

- [ ] Roll animation / `ANIM OFF` (7 checks): cards and header BPM roll frames,
      retargeting, snapping on page/track/palette/engine changes.
- [ ] Piano roll rendering (6 checks): ACID bars, 4-note chords, LEN 32 page 2,
      follow animation, held-key strip accent.
- [ ] `HOME > TRACKS` layout (3 checks): step rows, playing-step highlight,
      LARGE card labels.
- [ ] HELP hints (2 checks): every page/layer has a hint that fits its row;
      HELP ON/OFF timing.
- [ ] SONG timeline glide (1 check): ANIM ON follows the cursor block, then holds.
- [ ] `#181` redraw (1 check): PRESETS / ALGORITHM on MIXER, HOME, HOME > TRACKS,
      ENV in FLAT / LINE / LARGE with no blank frame.

Acceptance: `./build/host/ui_test` exits 0, with `project_test` and `regress`
still green.

## 2. Release v1.8.4

- [ ] Build the firmware and package (`./build.sh` / `tools/build.py`).
- [ ] Flash the device and smoke-test the fixes:
  - legacy projects with old drum tracks migrate to the dedicated DRUM track;
  - SONG four-slot chain behavior and SAVE → USER from HOME;
  - EDIT on AUTOMATION, EDIT on STEP, PRESETS on 8x8 STEP;
  - GREY palette, drum-lane mute behavior.
- [ ] Update `README.md` “New in 1.8.4”.
- [ ] Rebuild the web workbench/emulator package and publish `gh-pages`.
- [ ] Verify the live workbench shows the new version and updater line.

## 3. FUN11 — the fifth track joins SONG

The README’s SONG section states: “The DRUM track joins SONG with the next chain
format.” That is the FUN11 gate.

- [ ] Extend `CHAIN_NTRK` from 4 to 5 and `chain_row_t.slot[]` to five lanes.
- [ ] Add the fifth SONG lane in the timeline UI (draw, cursor, KNOB 2 track
      selection, keys A–D + DRUM).
- [ ] Bump the project format (FUN11) and keep FUN10/older reads:
      old 4-slot chains load with the DRUM lane silent.
- [ ] Update `chain_prepare` / `chain_apply` / `chain_clock` for five lanes and
      the DRUM track’s patterns.
- [ ] Extend tests: chain, song lanes, song pictures, SONG timeline, project
      round trips and migration; rebaseline goldens/CPU where needed.
- [ ] Update README and the manual pages for SONG’s fifth lane.

## 4. Phase 2 — product features

Not yet specified in this repository. Add the agreed phase-2 scope here before
starting implementation so the test/release gates stay explicit.

## Verification commands

```sh
# UI suite (current 20 visual failures until step 1 is done)
cc -w -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm
./build/host/ui_test

# Focused groups while working
UI_TEST_ONLY=sound_loads ./build/host/ui_test
UI_TEST_ONLY=grid ./build/host/ui_test
UI_TEST_ONLY=song_lanes ./build/host/ui_test

# Project formats and regression
cc -w -Ibuild/gen -Ifirmware/src -o build/host/project_test tests/project_test.c -lm
./build/host/project_test
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm
./build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

## References

- `README.md` — feature list, “New in …” release notes, SONG limitations.
- `firmware/src/core.h` — `CHAIN_NTRK`, `chain_row_t`, `NPART` / `NTRK`.
- `firmware/src/song_chain.c` — SONG lanes and playback.
- `firmware/src/project.c` — project formats and legacy migration.
- `tests/ui_test.c`, `tests/project_test.c`, `tests/regress.c` — the gates.
