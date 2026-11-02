# Next session — handoff (2026-11-01)

Status: source + emulator work committed and deployed; hardware validation still blocked
(no working FM-1). This note is the starting point for the next session.

## Where things stand

- **`main`** (source): `689f83b` firmware control-surface · `66e2979` emulator MIDI out / XY /
  control surface · `d1a9a45` skin colors · `23e752a` skins · `8bedd31` Android integration doc ·
  `2c13555` loader safety.
- **`gh-pages`**: `ee08bb9` — workbench with MIDI out, XY pad and the real firmware
  control-surface mode. The public **installer package was deliberately not updated** with the
  untested firmware/loader.
- Docs: [`ANDROID-INTEGRATION.md`](ANDROID-INTEGRATION.md) (design, CC map, test rig) and
  [`SAFETY-CHECKLIST.md`](SAFETY-CHECKLIST.md) (update-safety validation/follow-ups).

## Done this session

1. **FM-1 update safety** — loader red/green screens, flashing red RECONNECT, 30 s → mask-ROM
   UBOOT, red app-side screen, docs, web-installer first-flash messaging. Built + host-tested;
   needs a real unit.
2. **Emulator case skins** — Black, Dark Green (black case + mint keys), Orange, White, Pink,
   White/Blue, Purple, each a **case + keybed** pair extracted from reference photos. Key glow
   stays clean white.
3. **Emulator MIDI out** — `web_midi_out()` drains the firmware's MIDI-out ring; the worklet
   forwards packets; a **MIDI LINK** panel selects a Web MIDI output.
4. **XY modulation pad** — canvas pad under the emulator, sends two CCs to the emulated FM-1 and
   the selected MIDI out; defaults X = CC 74 (cutoff), Y = CC 71 (resonance), editable to 20/21.
5. **Firmware control-surface mode** (`FELUCCA_CTL`, off by default) — KNOB 1–4 → CC 22–25
   (SCL held → 26–29), buttons → CC 102–111, keys → notes 53–79; `trk_note_on()` muted while
   active. The emulator toggle calls `web_ctl_surface()`, so the workbench runs the real
   firmware path (not a JS mock).

## Verified

- `./build.sh` (app + loader + package) passes; `web/emu/build.sh v1.8.4.4` passes.
- `ui_test` all passed; `regress` **101 golden renders, 0 changed**, 0 voice/routing/CPU failures.
- Browser: boots, skin picker, control-surface toggle + keys, no console errors.
- `web_ctl_surface` / `web_midi_out` present in the built WASM.

## Next session (priority order)

1. **Close the loop without the phone**: run Loom Groovebox's **desktop build** on the Mac
   (`composeApp/src/desktopMain`, `javax.sound.midi`) and connect it to the emulator through
   **macOS IAC virtual MIDI** (Audio MIDI Setup → MIDI Studio → IAC Driver). Select IAC as the
   emulator's **Out**, enable **Control surface**, play buttons/knobs/keys.
2. **Android receiver UI** (Loom Groovebox): the highlight layer showing which on-screen knobs
   the FM-1's 4+4 knobs drive, the **bank scroll** (CC 30) mapping, and the button → screen
   actions (FX, HOME, SAVE, ENV, LFO, EDIT, GLO, ARP, PLAY, REC; ARP long-press = on/off).
3. **Hardware trigger** for `ctl_surface`: a MENU setting + the SysEx handshake (the emulator
   path is wired; the on-device trigger is not).
4. **Phone transport** (later): USB-MIDI interface pair (phone OTG → DIN → Mac) or a **Pico
   USB-MIDI bridge**. Note: macOS cannot be a USB MIDI device and Android USB MIDI is host-only,
   so a C-to-C cable does not work. BLE/network MIDI would need adding to Loom Android
   (`MidiManager` is `android.media.midi` only).
5. **Audio routing**: `Oboe setDeviceId()` to the FM-1's USB audio device, auto-pick when it is
   the only one, with a Loom settings override.
6. **Safety checklist hardware pass** once a working unit exists (see `SAFETY-CHECKLIST.md`).

## CC map (contract both sides share)

Continuous, CC 20–31: **20/21** XY X/Y (phone → FM-1); **22–25** KNOB 1–4; **26–29** KNOB 1–4
with SCL; **30** bank scroll. Buttons, CC 102–111: FX 102, HOME 103, SAVE 104, ENV 105, LFO 106,
EDIT 107, GLO 108, ARP 109, PLAY 110, REC 111 (127 = press, 0 = release). Keys = notes 53–79.

## Useful commands

```
./build.sh                                  # app + loader + package
web/emu/build.sh v1.8.4.4                   # emulator (needs emcc; uses build/gen)
python3 -m http.server 8790 -d build/emu    # local workbench
cc -w -Ibuild/gen -Ifirmware/src -o build/ui_test tests/ui_test.c -lm && ./build/ui_test
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/regress tests/regress.c -lm && \
  ./build/regress tests/golden.txt tests/cpu_baseline.txt
```

Deploying the workbench to `gh-pages`: worktree the branch, copy
`build/emu/{index.html,worklet.js,felucca.wasm}`, commit and push. Do **not** copy the `.fwsc`
package until the firmware/loader is hardware-validated.

## Gotchas

- The control-surface path now runs the firmware: toggling it in the emulator sends
  `{type:"ctl_surface"}` → `web_ctl_surface()`. Keep that wiring; don't reintroduce the JS-side
  CC emission (it would double-send).
- `FELUCCA_CTL` is defined only in `felucca.c` and `felucca_web.c`; host tests compile the
  control-surface code out, so `panel.c`/`ui_input.c` stay buildable there.
- The emulator's MIDI-out picker only lists devices the browser/OS exposes; in a headless test
  browser it shows only "(no output)" — that is expected.
- The FM-1 unit is dead; all firmware changes above are build/host-verified only.
