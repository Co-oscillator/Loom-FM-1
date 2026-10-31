# Loom FM-1 v1.8.4.4 — hardware validation

This is the delta checklist for v1.8.4.4. It moves the four EDIT shortcuts to
their own gestures and fixes the DRUM track's EDIT engine picker. The v1.8.4.3
and v1.8.4.2 checklists still apply for everything else.

## Release identity

- Version: **v1.8.4.4**
- Package: `build/loom-groovebox-1.8.4.4.fswc`
- SHA-256: `8f63762faabcf8e10758f0797dacc3ae8b5e6776a749c18879adfcd8901bbaf3`
- Package identity: `FM-1_985`

## Automated gates (green)

```sh
./build/host/ui_test
./build/host/project_test
./build/host/regress tests/golden.txt tests/cpu_baseline.txt
./build/host/fun10_test
./build/host/motion_test
./build/host/upreset_test
NOCOST=1 ./build/host/drum_test
```

## Flash

```sh
/tmp/fm1venv/bin/python tools/fm1_install.py build/loom-groovebox-1.8.4.4.fswc --yes
```

## 1. EDIT is a page button

- [ ] SEQ > STEP: tap **EDIT**. The EDIT page opens; the step is **not** cleared.
- [ ] SEQ > AUTOMATION: tap EDIT. The EDIT page opens; the selected row is **not** deleted.
- [ ] SEQ > DETAIL: tap EDIT. The EDIT page opens; CHANCE/RATCH/NUDGE/VEL are unchanged.
- [ ] SAVE > USER / PROJECT: tap EDIT. The EDIT page opens; the slot is **not** renamed.
- [ ] Double-tap EDIT: the EDIT layer locks (as the other page buttons do); the page stays.

## 2. New shortcuts

- [ ] **SEQ TOOLS > CLR STEP** (hold SEQ, G4): the cursor step and its locks are cleared, the cursor moves on. **SAVE held** restores the step and its locks.
- [ ] CLR STEP on an empty step says **NOTHING TO CLEAR** and makes no undo copy.
- [ ] **AUTOMATION:** on a record / CHANCE / RATCH / NUDGE row, **OCT+ twice** deletes it. While a song plays: **STOP TO EDIT**, nothing changes.
- [ ] **DETAIL:** hold a step (or use the cursor step) and press **OCT-**. CHANCE/RATCH/NUDGE/VEL return to their defaults; **SAVE held** undoes it.
- [ ] **SAVE > USER / PROJECT:** press **OCT-** on a used slot → NAME opens with the slot's name. On an empty slot: **EMPTY SLOT**.
- [ ] OCT- rename does not change the stored sound; the next SAVE prefills the new name.

## 3. DRUM EDIT picker

- [ ] Select the DRUM track, hold **EDIT**. The engine picker shows **one DRUM cell + INIT**, not a grid of identical DRUM cells.
- [ ] The other white keys do nothing there; INIT still opens its dialog.
- [ ] A melodic track's EDIT picker still shows the engine cells.

## 4. Regression

- [ ] The v1.8.4.3 DRUM checks (one lane per voice, decay, lane mutes) still hold.
- [ ] The v1.8.4.2 checks (4x4 grid, 8x8, GREY/MONO, SONG four lanes, SAVE/PRESETS) still hold.
- [ ] HELP ON shows the new hints for USER / PROJECT / DETAIL.

## Known limitations

- FUN11 is not in this build: SONG still has four pattern lanes, and the DRUM track has no automation or locks until FUN11.
- The full `tests/run_tests.sh` still has unrelated red suites (`midi_control_test`, `fm4_test`, `fm_sample`, `theme_test`).
- Modulator learn (hold LFO/ENV + turn a knob) is a backlog design, not in this build: [`MIDI-LEARN.md`](MIDI-LEARN.md).

## Result

Record pass/fail per section. If EDIT still performs a destructive action, stop
and report the page; the old path is not supposed to remain.
