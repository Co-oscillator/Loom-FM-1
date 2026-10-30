# Loom FM-1 v1.8.4.2 — hardware validation

This is the release-validation checklist for the v1.8.4.2 package. The automated
gates are green; the items below are the hardware checks that need the FM-1.

## Release identity

- Version: **v1.8.4.2**
- Package: `build/loom-groovebox-1.8.4.2.fswc`
- SHA-256: `5a89f2231a71180a8efad609042073d682aa97b13590c301cd1174ad955350ff`
- Package identity: `FM-1_985`
- Device under test: FM-1 on USB MIDI (port name contains `Felucca`)

## Automated pre-flight (already green)

```sh
./build/host/ui_test
./build/host/project_test
./build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

Expected: all pass; regress reports 0 health / voice / routing / CPU failures.

## Flash

Command line (needs `mido` + `python-rtmidi`; the release venv is at
`/tmp/fm1venv` if it still exists):

```sh
/tmp/fm1venv/bin/python tools/fm1_install.py build/loom-groovebox-1.8.4.2.fswc --yes
```

Or use the web installer at <https://co-oscillator.github.io/Loom-FM-1/>.
GitHub Pages can lag a push by a minute; hard-refresh if it still says
v1.8.4.1.

## 1. Boot and version

- [ ] The splash / MENU > INFO shows **v1.8.4.2**.
- [ ] `fm1_install.py --info` reports `FM-1_985 [running]`.
- [ ] No crash, no stuck note, panel and LEDs normal after boot.

## 2. 4x4 and 8x8 sequencers

- [ ] **SEQ > 4x4 STEP** shows the **4x4 sequencer grid**: 16 large cells per bank, 4 columns × 4 rows. It must **not** show the piano roll.
- [ ] Notes are filled cells, rests are rings, ties carry on, accents stand out.
- [ ] The cursor has corner marks; the playing step is clearly marked and moves with the play head.
- [ ] KNOB 1 moves the cursor; KNOB 2 changes the bank; LEN 32 shows steps 16..31 on page 2.
- [ ] **4x4 PAT** shows the same 4x4 grid and its KNOBs edit LEN / DIV / SWING / GATE.
- [ ] **8x8 STEP / 8x8 PAT** still show the 64-step matrix, unchanged.
- [ ] The DRUM track uses its grid (white keys = steps, black keys = lanes), unchanged.

## 3. Engine colours and palettes

- [ ] In the default **GREY** palette, changing engines changes the theme/accent colour (ANALOG, FM6, WAVETABLE, SAMPLE, DRUM, etc. each have their own).
- [ ] MENU > DISPLAY > COLOR can select the colour palettes; the engine colour follows.
- [ ] **MONO** is neutral (the only intentionally monochrome palette).
- [ ] GREY is a grey base with the engine colour, not a flat monochrome UI.

## 4. SONG (still four pattern tracks)

- [ ] HOME > SONG shows the four pattern lanes; the dedicated DRUM track is **not** in SONG yet (FUN11).
- [ ] Sections, slots A–D / `-`, repeats and the play head work.
- [ ] A silent lane (`-`) stays silent; the other lanes play their slots.
- [ ] SONG playback stops and restores the editable patterns, LEN/DIV and record arms.

## 5. SAVE, PRESETS, EDIT

- [ ] HOME > SAVE opens **USER**; after visiting PROJECT, HOME > SAVE still returns to USER.
- [ ] Saving over an occupied user slot asks before overwriting.
- [ ] PRESETS on **4x4 STEP** and **8x8 STEP** moves the step cursor; the sound and steps stay.
- [ ] PRESETS on 4x4 PAT / 8x8 PAT edits KNOB 1 (LEN), not the sound.
- [ ] **EDIT on STEP** clears the cursor step and moves on.
- [ ] **EDIT on AUTOMATION** deletes/resets the selected row; while a song plays it says STOP TO EDIT.
- [ ] EDIT on USER / PROJECT still renames the slot.

## 6. Projects and legacy migration

- [ ] Load a current project: sound, steps, SONG chain, automation and name come back.
- [ ] Load an older project with the old GM drum part (track 4) or SAMPLE PERC: it lands on the dedicated **DRUM** track with its steps, level, reverb and automation kept.
- [ ] Load a project with an FM6 track: the OP1 level and filter amount are not clamped or changed.
- [ ] A project save/load round trip keeps the SONG chain and the dedicated DRUM track.
- [ ] Power-cycle: projects, user presets and settings persist.

## 7. DRUM track and lane mutes

- [ ] EDIT on the DRUM track: the black keys mute/unmute lanes; the grid shows muted lanes dimmed.
- [ ] A muted lane is silent; unmuting brings it back.
- [ ] A lane muted mid-ring keeps its voice state (it rings on unheard), as a channel mute.
- [ ] A sound/preset load into the DRUM track clears the lane mutes.

## 8. Audio, MIDI, USB

- [ ] Keys, sequencer, arp and DRUM kits sound normally; no stuck notes.
- [ ] MIDI IN plays the selected track / channels; MIDI OUT and clock work.
- [ ] USB MIDI and USB audio enumerate; the FM-1 is visible to the editor/installer.
- [ ] The web updater sees v1.8.4.2 and can install it without a replug; `--info` answers immediately after the restart.
- [ ] No CPU overload / dropouts on a busy 4-track pattern.

## 9. Regression spot checks

- [ ] ANALOG, FM6, WAVETABLE, SAMPLE, SLICE, GRAIN, SCRUB, PHYS, DRUM each load and play.
- [ ] The ARP runs in its modes; the chord keys and SCL layer work.
- [ ] The FX layer, MOMENTARY and quick layers still open/close cleanly.
- [ ] SAVE held undo, project save, user preset save and pattern load work.
- [ ] The screen does not blank/flash on PRESETS or ALGORITHM (MIXER, HOME, HOME > TRACKS, ENV).

## Known limitations at this release

- FUN11 is **not** in this build: SONG is still the four pattern tracks; the DRUM track joins SONG with the FUN11 chain format.
- 4x4 STEP/PAT are the 4x4 sequencer grid; the piano-roll experiment is reverted.
- The legacy suites `fun10_test`, `motion_test`, `upreset_test` and `drum_test` are still red/stale. They are **not** part of the v1.8.4.2 hardware gate, but they are the FUN11 readiness work.

## Result

Record pass/fail per section and attach any failing screen/audio/MIDI notes. If a
section fails, do not start FUN11 until it is understood.
