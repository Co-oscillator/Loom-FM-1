# Loom FM-1 Roadmap — archived

> **Status: paused / archived (2026-10-31).** There is no working FM-1. The unit
> was lost during the v1.8.4.4 flash: the USB cable was disturbed during the
> write, the loader stopped enumerating, and the power switch was torn off while
> attempting the documented recovery. The owner cannot justify another FM-1
> while the platform has no hardware recovery button. See
> [`docs/HARDWARE-FAILURE.md`](docs/HARDWARE-FAILURE.md).
>
> The source, releases, tests, web emulator and format notes are preserved. This
> roadmap is kept so the work can resume if a suitable device or a hardware
> test rig becomes available.

## Why this is archived

- The FM-1 can be left black and non-enumerating by an interrupted write; there
  is no recovery button.
- Recovery needs the [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter)
  wired to the USB data lines, which requires opening a case that is not
  designed to be opened.
- Opening the case tore the small SMD power switch off the board. The switch is
  a commodity part, but a bench repair costs more than the device.
- A replacement FM-1 has the same recovery limitation, so another unit is not a
  justified risk for this project.
- No alternative device with a hardware recovery button was available at the
  time of archiving.

## Preserved work

- Firmware source: `firmware/` (application, HAL, update loader).
- Releases: v1.8.4.4 package and web updater on `gh-pages`.
- Host gates: `ui_test`, `project_test`, `regress`, `fun10_test`,
  `motion_test`, `upreset_test`, `drum_test` — all green at archive time.
- Web emulator: `web/emu/` (browser build, v1.8.4.4).
- FUN11 format/migration spec: [`docs/FUN11-FORMAT.md`](docs/FUN11-FORMAT.md).
- Modulator-learn design note: [`docs/MIDI-LEARN.md`](docs/MIDI-LEARN.md).
- Hardware failure postmortem and photos: [`docs/HARDWARE-FAILURE.md`](docs/HARDWARE-FAILURE.md).
- Validation checklists: [`docs/VALIDATION-1.8.4.4.md`](docs/VALIDATION-1.8.4.4.md),
  [`docs/VALIDATION-1.8.4.3.md`](docs/VALIDATION-1.8.4.3.md),
  [`docs/VALIDATION-1.8.4.2.md`](docs/VALIDATION-1.8.4.2.md).

## Why the emulator cannot replace hardware

The browser emulator is useful for the UI, audio engine and most page logic, but
it cannot cover the parts that need real panel input:

- key **long presses** (holds that open layers or the menu),
- **key combos** (two or more keys held together),
- **key + knob combos** (hold a step/key and turn a knob),
- **menu-button + keyboard combos** (e.g. hold EDIT and press a key),
- timing-sensitive gestures (tap vs hold, double taps, release actions),
- real USB enumeration/recovery and the bootloader path.

Firmware development therefore needs either a physical unit or a
hardware-in-the-loop rig that can drive the panel matrix and knobs. The host
tests remain valuable, but they are not a substitute for the panel.

## What would revive this

- A working FM-1 (or a donor board) plus a stable flashing setup.
- A future device with a **USB DFU/recovery button** or a documented ROM boot
  mode that always enumerates.
- A hardware-in-the-loop test rig for the FM-1 panel (key matrix, encoders,
  long presses/combos) so the firmware can be developed without risking the only
  unit.
- A port target with a similar display/panel/audio architecture, using the
  preserved format spec and host tests.

## Archive: completed stabilization and releases

- [x] `ui_test` green (4x4 grid, HOME > TRACKS, HELP hints, SONG glide, #181
      redraw, palette/theme handling).
- [x] `project_test`, `regress` (goldens, health, voices/routing, CPU) green.
- [x] v1.8.4.2: 4x4 STEP/PAT restored, GREY engine colours, SAVE/EDIT/PRESETS,
      legacy drum migration, DRUM lane mutes.
- [x] v1.8.4.3: DRUM ANLG/FM one-lane-per-voice fix, exact five-track project
      round trips, legacy suites green, FUN11 format spec.
- [x] v1.8.4.4: EDIT is a page button; CLR STEP / OCT- shortcuts; one DRUM picker
      cell; published but **not flashed** (the unit failed during the attempt).
- [x] Web workbench/updater v1.8.4.4 published to `gh-pages`.
- [ ] Hardware validation of v1.8.4.4 — **paused** (no working unit).

## Archive: FUN11 readiness (host work done, hardware gate paused)

- [x] Repair `fun10_test` (FUN7/FUN9 retained slots; five-track format).
- [x] Repair `motion_test` (current ARP modes; roll/DRUM-grid locks; migration).
- [x] Repair `upreset_test` (user preset format/migration).
- [x] Repair `drum_test` (`DK_STD` / `DK_10` kit names; ANLG/FM path).
- [x] Write the FUN11 format/migration spec.
- [ ] Widen the motion record to five tracks (FUN11).
- [ ] Run the v1.8.4.4 hardware checklist.

## Archive: FUN11 — the fifth track joins SONG

The README’s SONG section states: “The DRUM track joins SONG with the next chain
format.” That is the FUN11 gate. None of the hardware-facing FUN11 work was
started; the format spec is ready.

- [ ] Extend `CHAIN_NTRK` from 4 to 5 and `chain_row_t.slot[]` to five lanes.
- [ ] Add the fifth SONG lane in the timeline UI (draw, cursor, KNOB 2 track
      selection, keys A–D + DRUM).
- [ ] Bump the project format (FUN11) and keep FUN10/older reads: old four-slot
      chains load with the DRUM lane silent.
- [ ] Update `chain_prepare` / `chain_apply` / `chain_clock` for five lanes and
      the DRUM track’s patterns.
- [ ] Extend tests: chain, song lanes, song pictures, SONG timeline, project
      round trips and migration; rebaseline goldens/CPU where needed.
- [ ] Update README and the manual pages for SONG’s fifth lane.

### FUN11 scope carried from the v1.7 planning notes

- [ ] **Persist ARP patterns with the project** (currently RAM only; `arp_pat`
      is not in `project.c` / `upreset.c`).
- [ ] **Pattern bank:** 32–64 patterns, SONG rows beyond 16, and a
      “queue pattern at next bar” operation.
- [ ] **New INFO capability tags**, migration from FUN10, and a decision on the
      backup format.
- [ ] Keep the existing FUN10/older reads and the dedicated fifth track’s
      migration rules.

## Archive: backlog after FUN11

- **Modulator learn (LFO / ENV):** hold LFO or ENV, turn the knob of the
  parameter on the active page, release to assign it. It replaces the fixed
  destination list and works for any knob-bearing page. Design note:
  [`docs/MIDI-LEARN.md`](docs/MIDI-LEARN.md).
- **ANLG kit character:** tune the default punch/snap/brightness after a
  listening pass.
- **Remaining red suites:** `midi_control_test`, `fm4_test` / `fm4_div0_test`,
  `fm_sample`, `theme_test` (gfx include). These predate the archive and are not
  part of the main gate.

## Archive: web editor (`web/editor.html`)

- [ ] Teach the editor the firmware’s 114-parameter table so its grouped pages
      (ENV, ARP, …) return instead of one flat list; add the new ENV pages and
      ARP PAT.
- [ ] CSS: fix the overlapping step-table headers (“Chance %” / “Ratchet”) and
      the right-edge overflow; widen the main area on large screens and stack or
      scroll the two step tables.
- [ ] Installer: the progress bar is fixed; the “device did not come back”
      false failure after a successful flash is still unfixed.

## Archive: Loom Groovebox / Android (separate repository)

Status from the v1.7 notes; re-check the separate repository before starting.

- [ ] FM-1 XY pad screen and macro assignment over SysEx.
- [ ] Gesture → automation.
- [ ] Preset / backup library.
- [ ] On-phone firmware updater.
- [ ] Launchkey testing.
- [ ] Pitch-tracker melody sequencer.

## Archive: Phase 2 — product features

Not specified. Add the agreed phase-2 scope here before starting, so the
test/release gates stay explicit.

## Archive: appendix — reconciled v1.7 planning notes

These were captured from earlier conversations and checked against the tree.
The notes’ `97efcd4` / v1.7 state is stale.

- **Held-ARP overlay:** `LAYER_ARP` and `graph_arp` exist; the card was verified
  visually in the step-1 pass.
- **VOIC under PROG:** fixed — `chord.c` passes `P_VOIC` into
  `loom_chord_generate`.
- **Factory presets too wet:** mostly addressed; a taste audit remains.
- **Dead code:** the `if (0 && …)` label block is gone.
- **FUN11 / web editor / Android:** moved to the archive sections above.

## Archive: verification commands

```sh
cc -w -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm
./build/host/ui_test

UI_TEST_ONLY=sound_loads ./build/host/ui_test
UI_TEST_ONLY=grid ./build/host/ui_test
UI_TEST_ONLY=song_lanes ./build/host/ui_test

cc -w -Ibuild/gen -Ifirmware/src -o build/host/project_test tests/project_test.c -lm
./build/host/project_test
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm
./build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

## Archive: references

- `README.md` — feature list and hardware caution.
- `firmware/src/core.h` — `CHAIN_NTRK`, `chain_row_t`, `NPART` / `NTRK`.
- `firmware/src/song_chain.c` — SONG lanes and playback.
- `firmware/src/project.c` — project formats and legacy migration.
- `tests/ui_test.c`, `tests/project_test.c`, `tests/regress.c` — the gates.
- `docs/HARDWARE-FAILURE.md` — the failure that paused the project.
- `docs/FUN11-FORMAT.md` — the next format change.
- `docs/MIDI-LEARN.md` — modulator learn backlog.
