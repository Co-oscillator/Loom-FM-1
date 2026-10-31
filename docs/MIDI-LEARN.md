# Modulator learn (LFO / ENV) — design note

Status: **backlog** (after FUN11). Not in v1.8.4.4.

## Idea

Hold **LFO** or **ENV**, turn the knob of a parameter on the page that is
showing, and let go. That parameter becomes the held modulator's destination.
It replaces the fixed destination list (LFO DEST / ENV DEST) with "the knob you
just touched", so any parameter that a page exposes can be modulated — FX,
instrument, filter, mixer, sends, macros.

The page you navigate to first decides which parameters the knobs carry; the
learn gesture then connects the one you turn.

## Interaction

- **Hold LFO** (or ENV): the layer opens as it does today (momentary LFO, ENV
  destination). The footer says `TURN A KNOB TO ASSIGN`.
- **Turn a knob** on the active page: the page's KNOB 1..4 parameter is picked,
  with its track and its current value. The modulator previews at the current
  amount (a short ramp, not a jump).
- **Let go**: commit the assignment. The destination list shows the parameter
  (name + amount) and the modulator is routed.
- **Release without turning**: cancel, as today's momentary gesture.
- **OCT- while held**: undo the pick / clear the assignment.
- **A second LFO/ENV hold on the same parameter**: re-pick it (no duplicate
  entries).

## Scope

- Any parameter that is (a) motion-recordable and (b) exposed by a knob on a
  page. This is the same set the parameter locks use (`motion_param`).
- The picked parameter keeps its page and track: `KNOB 3` on `FX > INSERT`
  assigns the INSERT's type/mix, not a global.
- The modulator amount is the knob's current delta or a fixed default; the
  design should show the amount and let KNOB 4 trim it before release.

## Implementation sketch

1. Track the last knob touched in the current UI pass: `(track, param id,
   value)` — the same tuple `motion_capture` sees.
2. On LFO/ENV hold, arm a `learn_pick` flag. On the next knob turn, store the
   tuple and preview the modulation.
3. On release, write the destination into the LFO/ENV destination slots (the
   existing `P_LD_*` / `P_ED_*` parameters or the macro destination table) and
   refresh the page.
4. Do not record the preview as automation; it is a performance gesture like a
   sound load (SAVE held undo, no motion event).
5. Persist the assignment with the sound/project as the destination parameters
   already are; no new format field is needed unless the destination list grows.

## Open questions

- Which amount: the knob's current value, a fixed 50 %, or a preview-then-trim?
- ENV already has a fixed destination list (FILT/PIT/SHP/AMT). Does learn replace
  it or add a fifth "LEARNED" destination?
- LFO has four destinations (PIT/FLT/SHP/AMP). Learn should fill the first empty
  one, or replace the selected one.
- MIDI CC learn (the existing MIDI LEARN mode) is a different feature: it maps a
  CC to a parameter. This note is about *modulator* assignment, not CC mapping.
