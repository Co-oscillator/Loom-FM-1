# Importing Loom / Helm / Surge presets

The FM-1 web editor can import patches from the Loom ecosystem into its local
library, then store them to a device user slot with the normal librarian tools.

**Library → “Loom / Helm / Surge…”** accepts:

| File | Source | Target engine |
|---|---|---|
| `.loom.json` / `.json` | Loom portable preset (schema 1) | its `engine` field (`subtractive` → SUBTRACT, `wavetable` → WAVETABLE) |
| `.helm` | Helm patch (JSON; factory bank CC-BY 4.0) | SUBTRACT |
| `.fxp` | Surge XT patch (VST2 chunk + XML scene A; GPLv3) | WAVETABLE |

The import adds the converted patch to the editor's library (tagged `loom`,
`import`), where it can be auditioned or stored to the device. No firmware
change is required.

## Mapping

FM-1 parameter ids follow `firmware/src/core.h` (current layout: P_COUNT 114,
P_E0 106). Values are written positionally; when a device reports a different
P_COUNT the editor's normal count-based remapping applies.

**Common**

| Loom | FM-1 |
|---|---|
| amp ADSR (`100..103`, or `454..457` for Wavetable) | `ATK` `DEC` `SUS` `REL` |
| filter env amount (`118` / `464`) | `FLT` (0..63) |
| glide (`355`) | `GLD` / `GLID` |
| detune (`106` / `451`) | `DTUNE` / `DETN` |

**Subtractive → SUBTRACT**

| Loom | FM-1 E |
|---|---|
| osc shape (`104`) | WAVE |
| fold (`180`) | FOLD |
| osc levels / mix (`107/108`) | MIX |
| cutoff (`1`) | CUT |
| resonance (`113`) | RES |
| drive (`170`) | DRV |

**Wavetable → WAVETABLE**

| Loom | FM-1 E |
|---|---|
| morph (`450`) | POS |
| warp (`465`) | WARP |
| detune (`451`) | DETN |
| bits (`475`, 0 = clean) | BITS (inverted) |
| sample rate (`476`) | SRAT |
| drive (`467`) | DRV |
| glide (`355`) | GLID |
| `wavetableName` | SRC (PIANO/FLUTE/SAX/BREAK/USR1–3, otherwise BASIC) |

**Helm** is mapped through the same Loom parameter ids (oscillators, sub, noise,
cutoff → CUT, resonance, filter env, ADSRs, glide) and lands on SUBTRACT.

**Surge** is mapped through the Loom Wavetable ids (oscillator position/warp/
detune/waveshaper, filter type and cutoff → FLT, filter env, both envelopes) and
lands on WAVETABLE.

## Limitations

- FM-1's WAVETABLE has no base-cutoff parameter; the filter is driven by the
  filter envelope and LFO. A Loom/Surge base cutoff therefore becomes the FLT
  amount (approximate).
- FM-1 uses its own wavetable sources, so external wavetable names map to the
  closest built-in source, not the original table.
- Helm's unison voices, cross-mod, step sequencer, LFO routing and effects are
  not mapped. The same is true for Surge's modulation matrix, extra oscillator
  models and effects.
- Loom's FM/Analog drum kits and the other engines are not imported yet.
- Imported sounds keep attribution in the library tags/source notes where the
  file provides it; Helm factory patches are CC-BY 4.0 and Surge XT is GPLv3.
