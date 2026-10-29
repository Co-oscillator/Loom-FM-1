/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Loom Pi / Loom FM-1 Contributors */
#ifndef LOOM_CHORD_H
#define LOOM_CHORD_H

#include <stdint.h>
#include <stdbool.h>

#define LOOM_CHORD_STEPS 8
#define LOOM_CHORD_MAX_NOTES 6

typedef enum {
    LOOM_COMPLEXITY_SIMPLE = 0,    /* Triads (3 notes) */
    LOOM_COMPLEXITY_COMPLEX = 1,   /* 7ths / 9ths (5 notes) */
    LOOM_COMPLEXITY_COLTRANE = 2,  /* Coltrane changes / modal jazz */
    LOOM_COMPLEXITY_COUNT = 3
} loom_complexity_t;

typedef enum {
    LOOM_VOICE_CLOSE = 0,   /* root position (chord.c VC_CLOSE) */
    LOOM_VOICE_OPEN = 1,    /* the second tone an octave up (VC_OPEN) */
    LOOM_VOICE_INV1 = 2,    /* the lowest tone an octave up (VC_INV1) */
    LOOM_VOICE_INV2 = 3,    /* the two lowest tones (VC_INV2) */
    LOOM_VOICE_BASS = 4,    /* the root an octave down; a seventh drops its fifth (VC_BASS) */
    LOOM_VOICE_COUNT = 5
} loom_voicing_t;

typedef enum {
    LOOM_MOOD_CALM = 0,
    LOOM_MOOD_HAPPY = 1,
    LOOM_MOOD_SAD = 2,
    LOOM_MOOD_SPOOKY = 3,
    LOOM_MOOD_ANGRY = 4,
    LOOM_MOOD_EXCITED = 5,
    LOOM_MOOD_GRANDIOSE = 6,
    LOOM_MOOD_TENSE = 7,
    LOOM_MOOD_COUNT = 8
} loom_mood_t;

typedef struct {
    uint8_t count;
    uint8_t notes[LOOM_CHORD_MAX_NOTES];
    uint8_t degree;       /* 1..7 Roman numeral degree */
    uint8_t root_note;    /* Base root MIDI note */
} loom_chord_t;

typedef struct {
    loom_chord_t chords[LOOM_CHORD_STEPS];
    uint8_t root_note;
    uint8_t mood;
    uint8_t complexity;
    uint8_t active_step;
} loom_chord_prog_t;

/* Generate an 8-chord progression using Loom music theory rules, every chord voiced by `voicing` */
void loom_chord_generate(
    loom_chord_prog_t *out,
    uint8_t root_note,
    const uint8_t *scale_intervals,
    uint8_t scale_len,
    loom_mood_t mood,
    loom_complexity_t complexity,
    loom_voicing_t voicing,
    const uint8_t *anchors,
    uint8_t anchor_count,
    uint32_t *rng_state
);

/* Returns human-readable mood name */
const char *loom_chord_mood_name(loom_mood_t mood);

/* Returns human-readable complexity name */
const char *loom_chord_complexity_name(loom_complexity_t complexity);

#endif /* LOOM_CHORD_H */
