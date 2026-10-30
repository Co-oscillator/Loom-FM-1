# Loom FM-1 Roadmap

This is the working roadmap for the Loom FM-1 firmware. It records the current
stabilization state, the release gate, and the next format change (FUN11).
Phase 2 product scope is intentionally left open until it is defined.

## Current state

- Device firmware: **v1.8.3**
- Repository: `main` at the stabilization commits after `67973e0`
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

### FUN11 scope carried from the v1.7 planning notes

- [ ] **Persist ARP patterns with the project** (currently RAM only; `arp_pat`
      is not in `project.c` / `upreset.c`).
- [ ] **Pattern bank:** 32–64 patterns, SONG rows beyond 16, and a
      “queue pattern at next bar” operation, so the device performs while the
      phone composes.
- [ ] **New INFO capability tags**, migration from FUN10, and a decision on the
      backup format.
- [ ] Keep the existing FUN10/older reads and the dedicated fifth track’s
      migration rules.

## 4. Web editor (`web/editor.html`)

- [ ] Teach the editor this firmware’s 114-parameter table so its grouped pages
      (ENV, ARP, …) return instead of one flat list; add the new ENV pages and
      ARP PAT.
- [ ] CSS: fix the overlapping step-table headers (“Chance %” / “Ratchet”) and
      the right-edge overflow; widen the main area on large screens and stack or
      scroll the two step tables.
- [ ] Installer: the progress bar is fixed; the “device did not come back”
      false failure after a successful flash is still unfixed.

## 5. Loom Groovebox / Android (separate repository)

Status from the v1.7 notes; re-check the separate repository before starting.

- [ ] FM-1 XY pad screen and macro assignment over SysEx.
- [ ] Gesture → automation.
- [ ] Preset / backup library.
- [ ] On-phone firmware updater.
- [ ] Launchkey testing.
- [ ] Pitch-tracker melody sequencer.

## 6. Phase 2 — product features

Not yet specified in this repository. Add the agreed phase-2 scope here before
starting implementation so the test/release gates stay explicit.

## Appendix — reconciled v1.7 planning notes

These were captured from the earlier conversation and checked against the current
tree. The notes’ `97efcd4` / v1.7 state is stale; current `main` is `67973e0`
and the device is on v1.8.3.

- **Held-ARP overlay:** the current tree has `LAYER_ARP` (`ui_layer.c`) and the
  ARP note/pattern graph (`graph_arp` in `ui_graph.c`). Verify the card visually
  in the step-1 pass; it is no longer obviously blank.
- **VOIC under PROG:** fixed — `chord.c` passes `P_VOIC` into
  `loom_chord_generate` for `CH_PROG`.
- **Factory presets too wet:** mostly addressed in the current preset tables
  (many are fully dry; pads use light CHOR/REV). Remaining work is a taste audit
  across every engine, not a structural fix.
- **Dead code:** the disabled `if (0 && …)` label block is gone.
- **FUN11:** the four scoped items are in section 3.
- **Web editor:** in section 4.
- **Loom Groovebox / Android:** in section 5.

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
