# Loom FM-1 v1.8.4.3 — hardware validation

This is the delta checklist for v1.8.4.3. It fixes the DRUM lane rendering and
the project/motion seams found while repairing the legacy suites. The v1.8.4.2
checklist still applies for everything not listed here.

## Release identity

- Version: **v1.8.4.3**
- Package: `build/loom-groovebox-1.8.4.3.fswc`
- SHA-256: `89e753bb9e559f9eb790ecc06f7d3d184a3adb5eeaddf566698a972216459272`
- Package identity: `FM-1_985`
- Device under test: FM-1 on USB MIDI (port name contains `Felucca`)

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

Expected: all pass; regress reports 0 changed goldens, 0 health / voice /
routing / CPU failures. The goldens changed once for the DRUM lane fix and are
committed with this release.

## Flash

```sh
/tmp/fm1venv/bin/python tools/fm1_install.py build/loom-groovebox-1.8.4.3.fswc --yes
```

If `--info` says **FM-1 not found**, power-cycle the FM-1 with the USB cable
connected (it enumerates at boot), close any app holding the `Felucca` port, and
run `--info` again.

## 1. DRUM kits ANLG / FM (the fix)

- [ ] Select **ANLG** and play one lane (KICK). Its level is the designed level, not a stack of copies.
- [ ] Play all eight lanes at once. The kit is loud but not eight times one lane; there is no clipping or limping envelope.
- [ ] The decay of a long lane (open hat / cymbal) is the designed decay, not eight times faster.
- [ ] EDIT on the DRUM track: mute KICK. Only the kick is silent; the other lanes still play.
- [ ] Mute a lane mid-ring: it keeps ringing on unheard (channel mute), and unmuting brings it back.
- [ ] Switch to **FM** and repeat the first three checks.
- [ ] Load a sound/preset into the DRUM track: the lane mutes clear.

## 2. DRUM track locks / automation

- [ ] SEQ > STEP on the DRUM track: hold a step and turn KNOB 1. The footer says **NO LOCKS ON DRUM**.
- [ ] No lock appears on step 1 (track 1) either: the old alias is gone.
- [ ] AUTOMATION on the DRUM track shows no new records; the other four tracks still record and play locks.
- [ ] On a melodic track, a lock still records, plays and clears (EDIT / SAVE held undo).

## 3. Projects and migration

- [ ] Save a project with a DRUM pattern (kit, level, reverb, steps) and load it: the same kit, level, reverb and steps return.
- [ ] The DRUM track's ENV values may show their defaults after the load (the DRUM hit is its envelope); this is expected.
- [ ] Load an old four-track project with a GM drum part or SAMPLE PERC: it lands on the DRUM track, and the SONG still has four lanes.
- [ ] Save/load a current project twice: no drift in the chain, the steps or the motion.
- [ ] Power-cycle: projects, user presets and settings persist.

## 4. The v1.8.4.2 checks still apply

Run the sections in [`VALIDATION-1.8.4.2.md`](VALIDATION-1.8.4.2.md) that cover
the 4x4 / 8x8 sequencers, GREY/MONO palettes, SONG (four lanes), SAVE / PRESETS
/ EDIT, audio / MIDI / USB, and the regression spot checks. The only expected
difference is the DRUM level/decay and the `NO LOCKS ON DRUM` message.

## Known limitations at this release

- FUN11 is **not** in this build: SONG still has four pattern lanes; the DRUM
  track joins SONG with FUN11.
- The dedicated DRUM track has **no automation or parameter locks** until FUN11
  widens the motion record (two track bits today). The guard is deliberate.
- The full `tests/run_tests.sh` still reports unrelated red suites that predate
  this release (`midi_control_test`, `fm4_test` / `fm4_div0_test`,
  `fm_sample` and `theme_test`'s `gfx.c` include). They are **not** part of the
  v1.8.4.3 gate; the four legacy suites and the core gates above are.

## Result

Record pass/fail per section. If the DRUM level or decay still sounds doubled,
stop and report the kit, the lane and the pattern; do not start FUN11.
