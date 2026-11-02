# Loom Android ↔ FM-1 integration

Two complementary directions over one USB link. Both are control + audio; the FM-1
already has most of the transport.

## What already exists (transport)

- **FM-1 firmware** already enumerates as a composite USB device: **USB MIDI in/out**
  (`firmware/src/usb.c`, `midi_out_event`) and **USB audio input** (UAC, stereo
  44.1/48 kHz — `FELUCCA_UAC`), plus TRS MIDI in. It has MIDI note-out (sequencer),
  MIDI CC **in** (`midi_control.c`: a fixed CC map + MIDI-learn to any parameter),
  and a full SysEx editor protocol for every parameter (`web/EDITOR_PROTOCOL.md`,
  `firmware/src/editor.c`).
- **FM-1 does not** currently emit CC on knob/button movement, and it has **no BLE**
  stack (BLE MIDI is stock-firmware-only). USB OTG is therefore the transport; BLE is
  out of scope unless deliberately added.
- **Loom Android** has a full MIDI stack (`com.groovebox.midi`: `MidiManager` with
  input/output devices, `MidiParser` for notes + CC, send/receive channels, MIDI
  clock) and Oboe low-latency audio (output + input streams).

So the link is: **phone (USB host) ↔ FM-1 (USB device)** carrying MIDI both ways and
digital audio one way (phone → FM-1).

## Feature 1 — phone XY pad → FM-1 parameters

- The phone shows a 2D pad; touch sends **two MIDI CCs** (X and Y) over USB MIDI.
- The FM-1's existing **MIDI-learn** maps any CC to any parameter, so X/Y are just two
  learned CCs → two parameters (e.g. cutoff + resonance, or two macro targets).
- Optional polish: the FM-1 echoes parameter values back via the existing SysEx editor
  protocol so the phone can show the current value and detents.

New code: only the phone-side pad + CC send; the FM-1 side is already capable.

## Feature 2 — FM-1 as a control surface for Loom Android

Phone synthesizes (Oboe); FM-1 sends MIDI notes/CC; phone audio plays through the FM-1's
speakers over USB audio. The FM-1 stops being a synth in this mode.

### Mode detection ("not a synthesizer")

- A persistent **MENU setting** toggles control-surface mode, plus a **MIDI handshake**
  (a small SysEx from the phone) to switch automatically and to confirm both sides agree.
- In this mode the FM-1 stops playing notes locally and instead **emits** MIDI notes
  (keys) and CCs (knobs/buttons). Emitting knob CCs is the main new FM-1 code path.

### Button / knob mapping (as specified)

| FM-1 control | Loom Android action |
|---|---|
| Left system knob 1 | Scroll/navigate between Loom engines |
| Left system knob 2 | Scroll the active knob-bank (which parameter set the 4+4 knobs control) |
| MASTER (volume) | Phone master volume (scales the passthrough audio) |
| KNOB 1..4 | Assignable parameters, bank 0 |
| KNOB 1..4 + **SCL** (shift) | Assignable parameters, bank 1 (8 total) |
| FX | FX screen |
| HOME | Play screen |
| SAVE | Save project dialogue |
| ENV, LFO | Patch screen |
| EDIT | Param screen |
| GLO | Settings screen |
| ARP | Arpeggiator popup; long-press = arp on/off |
| PLAY/STOP, REC | Loom transport |
| Keys / pads | MIDI notes → selected track |

### Knob banks + highlight

- The FM-1 sends a fixed, agreed CC map: knobs 1..4 + shifted 5..8, plus a **bank CC**
  from the bank-scroll knob.
- The phone owns the (bank, knob) → (screen section, parameter) mapping because it knows
  its layout; the FM-1 only sends "bank = N, knob k = value".
- The phone draws a **highlight layer** over the on-screen knobs currently bound to the
  FM-1, and re-maps them as the bank CC changes (Osc 1 → Osc 2 → … on the Param screen;
  compressor → bitcrush → … on the FX screen).

## What's new vs. what exists

| Side | New work |
|---|---|
| FM-1 | Control-surface mode flag + MENU item; CC-out on knob/button; XY already covered by MIDI-learn; volume passthrough is just the phone scaling its master. |
| Android | Route Oboe output to the FM-1's USB audio device id; XY pad + CC send; highlight layer + bank mapping; bind FM-1 CCs/notes to tracks via the existing MIDI stack. |

## Decisions (locked)

1. USB OTG only; no BLE for now.
2. Dedicated CC map (below).
3. Mode switch = a persistent MENU setting plus a SysEx handshake.
4. Audio: auto-pick the FM-1 as Loom's output when it is the only USB audio device and
   control-surface mode is active; a Loom settings item overrides the output device.
5. Order: XY pad (feature 1) first, control surface (feature 2) second.

## MIDI link — CC map and protocol

CCs are channel-agnostic (they never route to tracks); notes are keys only. The reserved
CCs avoid the FM-1's own fixed map (`5, 7, 10, 71–75, 91, 93, 94`), the MIDI-learn
"never learned" set (`0, 1, 6, 11, 32, 38, 64, 96–101, 120–127`), and the standard mode
messages.

### Continuous controls — CC 20–31 (standard "undefined" range)

| CC | Direction | Meaning |
|---|---|---|
| 20 | phone → FM-1 | XY pad **X** (0–127, 64 = centre) |
| 21 | phone → FM-1 | XY pad **Y** (0–127, 64 = centre) |
| 22–25 | FM-1 → phone | KNOB 1–4 (bank 0) |
| 26–29 | FM-1 → phone | KNOB 1–4 (bank 1, SCL held) |
| 30 | FM-1 → phone | bank scroll (absolute 0–127, wraps) |
| 31 | — | spare |

### Buttons / transport — CC 102–111 (standard "undefined" range)

Momentary: value 127 = press, 0 = release.

| CC | Button |
|---|---|
| 102 | FX |
| 103 | HOME |
| 104 | SAVE |
| 105 | ENV |
| 106 | LFO |
| 107 | EDIT |
| 108 | GLO |
| 109 | ARP (long-press = arp on/off; short = popup) |
| 110 | PLAY/STOP |
| 111 | REC |

### Keys

Notes on the FM-1's keyboard range go through Loom's existing note-to-track routing
(CH 1–4 → tracks 1–4, etc., per the current MIDI-in channel setting).

### Mode handshake (SysEx)

Phone → FM-1 to enter/exit control-surface mode. Proposed: a short vendor SysEx,
`F0 7D 54 01 <mode> F7` (mode 1 = control surface, 0 = synth), handled as a new editor
command (`firmware/src/editor.c`) or a small dedicated handler. The FM-1 acks so both
sides know the mode took; the MENU setting persists it, the handshake overrides per session.

### XY pad behaviour (feature 1)

- Phone sends CC 20 (X) and CC 21 (Y) continuously while a finger is down; release snaps
  back to 64 (centre) for both.
- The FM-1 maps them with the existing MIDI-learn (CC 20 → parameter, CC 21 → parameter),
  so feature 1 needs **no new FM-1 code**. A later nicety: a dedicated "XY" parameter pair
  (like the momentary macro) instead of MIDI-learn.

## Test rig (browser emulator)

The workbench has a **MIDI LINK** panel under the device:

- **MIDI out** — pick a Web MIDI output. The emulator forwards the firmware's queued MIDI
  (`web_midi_out`: notes/CC/clock) plus the control-surface CCs/notes to it. Point the phone's
  Loom app at the computer's MIDI port (BLE or network MIDI) to test the link.
- **XY MOD pad** — drag to send CCs to the emulated FM-1 (via `web_midi`) and to the MIDI out.
  Defaults: X = CC 74 (cutoff), Y = CC 71 (resonance); set them to 20/21 for the reserved
  contract.
- **Control surface (FM-1 → app)** — when on, the emulator's buttons emit CC 102–111, K1–K4
  emit CC 22–25 (SCL held: 26–29), and the keys emit notes F3 (53)..G5 (79), all to the MIDI out.

The **firmware side is implemented** behind `FELUCCA_CTL` (`firmware/src/panel.c`,
`ui_input.c`, `voice.c`): with `ctl_surface` set, the button, encoder and note edges emit the
CCs/notes above through `midi_out_event`, and `trk_note_on` stops the local synth. The emulator's
toggle sends `{type:"ctl_surface"}` to the worklet, which calls `web_ctl_surface()` — so the
emulator exercises the real firmware path. The hardware-side trigger (a MENU setting plus the
SysEx handshake) is still to be wired; the CC map above is the contract both sides share.
