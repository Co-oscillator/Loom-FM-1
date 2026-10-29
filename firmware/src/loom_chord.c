/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Loom Pi / Loom FM-1 Contributors */
#include "loom_chord.h"

static inline int loom_abs(int x) { return x < 0 ? -x : x; }

static const char *const MOOD_NAMES[LOOM_MOOD_COUNT] = {
    "CALM", "HAPPY", "SAD", "SPOOKY", "ANGRY", "EXCITED", "GRANDIOSE", "TENSE"
};

static const char *const COMPLEXITY_NAMES[3] = {
    "SIMPLE", "COMPLEX", "COLTRANE"
};

const char *loom_chord_mood_name(loom_mood_t mood) {
    if (mood < LOOM_MOOD_COUNT) return MOOD_NAMES[mood];
    return "UNKNOWN";
}

const char *loom_chord_complexity_name(loom_complexity_t complexity) {
    if (complexity <= LOOM_COMPLEXITY_COLTRANE) return COMPLEXITY_NAMES[complexity];
    return "UNKNOWN";
}

/* Fast 32-bit xorshift PRNG */
static uint32_t loom_prng(uint32_t *state) {
    uint32_t x = *state;
    if (!x) x = 0x2468ACE1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* 8 Moods, each with 2 variations of 8 Roman numeral scale degrees (1-indexed) */
static const uint8_t MOOD_DEGREES[LOOM_MOOD_COUNT][2][LOOM_CHORD_STEPS] = {
    /* 0: Calm */     { {1, 4, 1, 6, 4, 2, 5, 1}, {1, 4, 1, 4, 6, 2, 4, 1} },
    /* 1: Happy */    { {1, 5, 6, 4, 1, 2, 5, 1}, {1, 4, 5, 1, 6, 2, 5, 1} },
    /* 2: Sad */      { {6, 3, 4, 1, 2, 6, 5, 6}, {6, 4, 1, 5, 6, 4, 2, 6} },
    /* 3: Spooky */   { {1, 4, 2, 7, 1, 6, 5, 1}, {1, 2, 6, 7, 1, 4, 5, 1} },
    /* 4: Angry */    { {1, 6, 7, 1, 2, 6, 5, 1}, {1, 2, 1, 6, 7, 6, 5, 1} },
    /* 5: Excited */  { {1, 4, 5, 4, 6, 5, 1, 5}, {1, 6, 4, 5, 1, 4, 5, 1} },
    /* 6: Grandiose */{ {1, 5, 6, 3, 4, 1, 4, 5}, {1, 6, 3, 4, 1, 5, 1, 5} },
    /* 7: Tense */    { {7, 5, 2, 7, 5, 6, 7, 5}, {7, 2, 5, 7, 1, 2, 7, 5} }
};

/* Coltrane mood intervals */
static const uint8_t COLTRANE_CALM[5]    = {0, 5, 10, 14, 21};
static const uint8_t COLTRANE_HAPPY[5]   = {0, 4, 6, 7, 14};
static const uint8_t COLTRANE_SPOOKY[5]  = {0, 3, 6, 11, 13};
static const uint8_t COLTRANE_TENSE[6]   = {0, 4, 10, 13, 15, 18};
static const uint8_t COLTRANE_DEFAULT[5] = {0, 4, 7, 10, 14};

/* Bubble sort for small chord arrays (3-6 notes) */
static void sort_chord(loom_chord_t *chord) {
    uint8_t i, j, temp;
    for (i = 0; i < chord->count; i++) {
        for (j = i + 1; j < chord->count; j++) {
            if (chord->notes[j] < chord->notes[i]) {
                temp = chord->notes[i];
                chord->notes[i] = chord->notes[j];
                chord->notes[j] = temp;
            }
        }
    }
}

static void build_chord(
    loom_chord_t *chord,
    int base_root,
    const uint8_t *scale,
    uint8_t scale_len,
    loom_complexity_t complexity,
    loom_mood_t mood
) {
    uint8_t i;
    chord->count = 0;
    chord->root_note = (uint8_t)(base_root < 0 ? 0 : (base_root > 127 ? 127 : base_root));

    if (complexity == LOOM_COMPLEXITY_COLTRANE) {
        const uint8_t *iv = COLTRANE_DEFAULT;
        uint8_t n_iv = 5;

        switch (mood) {
            case LOOM_MOOD_CALM:   iv = COLTRANE_CALM;   n_iv = 5; break;
            case LOOM_MOOD_HAPPY:  iv = COLTRANE_HAPPY;  n_iv = 5; break;
            case LOOM_MOOD_SPOOKY: iv = COLTRANE_SPOOKY; n_iv = 5; break;
            case LOOM_MOOD_TENSE:  iv = COLTRANE_TENSE;  n_iv = 6; break;
            default:               iv = COLTRANE_DEFAULT; n_iv = 5; break;
        }

        chord->count = n_iv;
        for (i = 0; i < n_iv; i++) {
            int n = base_root + iv[i];
            chord->notes[i] = (uint8_t)(n < 0 ? 0 : (n > 127 ? 127 : n));
        }
        return;
    }

    /* Simple (3 notes: triad) or Complex (5 notes: 7th + 9th) */
    uint8_t notes_to_stack = (complexity == LOOM_COMPLEXITY_SIMPLE) ? 3 : 5;
    int root_in_octave = base_root % 12;
    int octave_shift = (base_root / 12) * 12;

    /* Find closest note in scale */
    int min_diff = 100;
    int root_idx = 0;
    for (i = 0; i < scale_len; i++) {
        int diff = loom_abs(root_in_octave - (int)scale[i]);
        if (diff < min_diff) {
            min_diff = diff;
            root_idx = i;
        }
    }

    chord->count = notes_to_stack;
    for (i = 0; i < notes_to_stack; i++) {
        int step_idx = root_idx + i * 2;
        int oct = (step_idx / (int)scale_len) * 12;
        int note = octave_shift + oct + scale[step_idx % scale_len];
        chord->notes[i] = (uint8_t)(note < 0 ? 0 : (note > 127 ? 127 : note));
    }
}

/* Smooth voice leading: minimize average pitch jump between consecutive chords */
static void apply_voice_leading(loom_chord_t *chord, const loom_chord_t *last_chord) {
    if (!chord->count || !last_chord->count) return;

    int sum_last = 0;
    uint8_t i;
    for (i = 0; i < last_chord->count; i++) sum_last += last_chord->notes[i];
    int avg_last = sum_last / last_chord->count;

    int sum_curr = 0;
    for (i = 0; i < chord->count; i++) sum_curr += chord->notes[i];
    int avg_curr = sum_curr / chord->count;

    int diff = avg_last - avg_curr;
    /* Integer rounding: (diff >= 0 ? (diff + 6) / 12 : (diff - 6) / 12) */
    int shift = (diff >= 0) ? (diff + 6) / 12 : (diff - 6) / 12;

    if (shift != 0) {
        int shift_semis = shift * 12;
        for (i = 0; i < chord->count; i++) {
            int n = (int)chord->notes[i] + shift_semis;
            if (n >= 0 && n <= 127) {
                chord->notes[i] = (uint8_t)n;
            }
        }
    }
}

/* Multi-Anchor: preserve common tones across progression */
static void apply_multi_anchor(loom_chord_t *chord, const uint8_t *anchors, uint8_t anchor_count) {
    if (!anchor_count || !chord->count) return;

    sort_chord(chord);
    int replacement_idx = (int)chord->count - 1; /* replace top notes down */

    uint8_t a, i;
    for (a = 0; a < anchor_count; a++) {
        uint8_t anchor_pc = anchors[a] % 12;
        bool found = false;

        for (i = 0; i < chord->count; i++) {
            if ((chord->notes[i] % 12) == anchor_pc) {
                found = true;
                break;
            }
        }

        if (!found && replacement_idx >= 0) {
            int target_octave = (chord->notes[replacement_idx] / 12) * 12;
            int n = target_octave + anchor_pc;
            if (n >= 0 && n <= 127) {
                chord->notes[replacement_idx] = (uint8_t)n;
                replacement_idx--;
            }
        }
    }

    sort_chord(chord);
}

/* The chord keys' voicings (chord.c VC_*), on a generated chord: OPEN the second tone an octave up, INV1 / INV2
 * the lowest one / two, BASS the root an octave down (a seventh drops its fifth). Ascending, inside 0..127,
 * each note once, as chord_make leaves them. Applied here so the progression's own players (the arp, the CHORD
 * page's keys) play the voicing too, not only a key's chord_make. */
static void apply_voicing(loom_chord_t *chord, loom_voicing_t voicing)
{
    int16_t t[LOOM_CHORD_MAX_NOTES + 1];
    uint8_t n = chord->count, i, j, out = 0;

    if (voicing <= LOOM_VOICE_CLOSE || voicing >= LOOM_VOICE_COUNT || !n) return;
    for (i = 0; i < n; i++) t[i] = chord->notes[i];

    if (voicing == LOOM_VOICE_OPEN) {
        if (n >= 3) t[1] = (int16_t)(t[1] + 12);
    } else if (voicing == LOOM_VOICE_INV1 || voicing == LOOM_VOICE_INV2) {
        for (j = 0; j < (voicing == LOOM_VOICE_INV2 ? 2u : 1u); j++) {
            int16_t low = (int16_t)(t[0] + 12);
            for (i = 1; i < n; i++) t[i - 1u] = t[i];
            t[n - 1u] = low;
        }
    } else {                                           /* LOOM_VOICE_BASS */
        if (n >= 4u) {                                 /* a seventh (or a ninth) drops its fifth */
            for (i = 2; i + 1u < n; i++) t[i] = t[i + 1u];
            n--;
        }
        for (i = n; i > 0; i--) t[i] = t[i - 1u];
        t[0] = (int16_t)(t[1] - 12);
        n++;
    }

    for (i = 1; i < n; i++)                            /* ascending */
        for (j = i; j > 0 && t[j - 1u] > t[j]; j--) {
            int16_t x = t[j];
            t[j] = t[j - 1u];
            t[j - 1u] = x;
        }
    for (i = 0; i < n; i++) {                          /* inside 0..127, each note once */
        if (t[i] < 0 || t[i] > 127) continue;
        if (out && chord->notes[out - 1u] == (uint8_t)t[i]) continue;
        chord->notes[out++] = (uint8_t)t[i];
    }
    if (!out) {
        chord->notes[0] = (uint8_t)(chord->root_note > 127 ? 127 : chord->root_note);
        out = 1;
    }
    chord->count = out;
}

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
) {
    if (!scale_len) return;
    if (mood >= LOOM_MOOD_COUNT) mood = LOOM_MOOD_CALM;
    if (voicing >= LOOM_VOICE_COUNT) voicing = LOOM_VOICE_CLOSE;

    out->root_note = root_note;
    out->mood = mood;
    out->complexity = complexity;
    out->active_step = 0;

    int baseline_root = root_note;
    if (anchor_count > 0) {
        int sum_anchor = 0;
        uint8_t a;
        for (a = 0; a < anchor_count; a++) sum_anchor += anchors[a];
        int avg_anchor = sum_anchor / anchor_count;
        int anchor_octave = ((avg_anchor + 6) / 12) * 12;
        if (anchor_octave < 24) anchor_octave = 24;
        if (anchor_octave > 96) anchor_octave = 96;
        baseline_root = (root_note % 12) + anchor_octave;
    }

    /* Pick Roman numeral degree variation (0 or 1) */
    uint8_t var_idx = (loom_prng(rng_state) >> 8) & 1u;
    const uint8_t *degrees = MOOD_DEGREES[mood][var_idx];

    loom_chord_t last_chord = {0};

    uint8_t i;
    for (i = 0; i < LOOM_CHORD_STEPS; i++) {
        uint8_t degree = degrees[i];
        bool is_tritone_sub = false;

        if (complexity == LOOM_COMPLEXITY_COLTRANE && (i == 2 || i == 5 || i == 6)) {
            if (degree == 5) {
                is_tritone_sub = true;
            }
        }

        int coltrane_shift = 0;
        if (complexity == LOOM_COMPLEXITY_COLTRANE) {
            if (i >= 2 && i < 4) {
                coltrane_shift = 4;
            } else if (i >= 4 && i < 6) {
                coltrane_shift = 8;
            }
        }

        int scale_idx = (degree - 1) % scale_len;
        int base_root = baseline_root + scale_intervals[scale_idx];
        if (is_tritone_sub) base_root += 6;
        base_root += coltrane_shift;

        loom_chord_t chord;
        build_chord(&chord, base_root, scale_intervals, scale_len, complexity, mood);
        chord.degree = degree;

        if (last_chord.count > 0) {
            apply_voice_leading(&chord, &last_chord);
        }

        if (anchor_count > 0) {
            apply_multi_anchor(&chord, anchors, anchor_count);
        }

        sort_chord(&chord);
        last_chord = chord;                            /* (voice leading follows the plain chord, not its voicing) */
        apply_voicing(&chord, voicing);
        out->chords[i] = chord;
    }
}
