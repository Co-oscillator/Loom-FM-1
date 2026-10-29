/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Loom Pi / Loom FM-1 Contributors */
#include "loom_arp.h"

static inline uint32_t loom_arp_rand(loom_arp_t *arp)
{
    arp->rng_state = arp->rng_state * 1664525u + 1013904223u;
    return arp->rng_state;
}

static void loom_arp_sort(uint8_t *arr, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++) {
        uint8_t key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static void loom_arp_roll_weird(loom_arp_t *arp)
{
    if (arp->weird == 0) {
        arp->rate_mult = 1;
        arp->speed_mult_q8 = 256;
        arp->is_weird = false;
        return;
    }

    uint32_t roll = loom_arp_rand(arp) % 128;
    arp->is_weird = (roll < arp->weird);

    if (!arp->is_weird) {
        arp->rate_mult = 1;
        arp->speed_mult_q8 = 256;
        return;
    }

    uint32_t r = loom_arp_rand(arp) % 100;
    if (arp->weird > 76) { /* > 0.6 */
        if (r < 25) arp->rate_mult = 7;
        else if (r < 55) arp->rate_mult = 4;
        else if (r < 85) arp->rate_mult = 2;
        else arp->rate_mult = (arp->weird >= 127) ? 4 : 1;
    } else if (arp->weird > 38) { /* > 0.3 */
        if (r < 45) arp->rate_mult = 4;
        else if (r < 85) arp->rate_mult = 2;
        else arp->rate_mult = 1;
    } else {
        if (r < 70) arp->rate_mult = 2;
        else arp->rate_mult = 1;
    }

    r = loom_arp_rand(arp) % 100;
    if (arp->weird > 76) {
        if (r < 25) arp->speed_mult_q8 = 768;      /* 3.0x */
        else if (r < 50) arp->speed_mult_q8 = 384; /* 1.5x */
        else if (r < 70) arp->speed_mult_q8 = 128; /* 0.5x */
        else if (r < 90) arp->speed_mult_q8 = 192; /* 0.75x */
        else arp->speed_mult_q8 = (arp->weird >= 127) ? 320 : 256;
    } else if (arp->weird > 38) {
        if (r < 45) arp->speed_mult_q8 = 384;      /* 1.5x */
        else if (r < 90) arp->speed_mult_q8 = 192; /* 0.75x */
        else arp->speed_mult_q8 = 256;             /* 1.0x */
    } else {
        if (r < 50) arp->speed_mult_q8 = 320;      /* 1.25x */
        else if (r < 90) arp->speed_mult_q8 = 218; /* 0.85x */
        else arp->speed_mult_q8 = 256;             /* 1.0x */
    }
}

static void loom_arp_generate_prog(loom_arp_t *arp)
{
    if (arp->chord_prog_enabled && arp->n_held > 0) {
        loom_chord_generate(&arp->prog,
                            arp->scale_root,
                            arp->scale_intervals,
                            arp->scale_len,
                            (loom_mood_t)arp->chord_mood,
                            (loom_complexity_t)arp->chord_complexity,
                            LOOM_VOICE_CLOSE,           /* (the tracks' VOIC is applied in chord.c's generator) */
                            arp->held,
                            arp->n_held,
                            &arp->rng_state);
    }
    arp->last_harmonic_step = -1;
}

static void loom_arp_update_sequence(loom_arp_t *arp)
{
    arp->seq_len = 0;
    if (arp->n_held == 0) {
        arp->last_harmonic_step = -1;
        return;
    }

    uint8_t base[LOOM_ARP_MAX_SEQ];
    uint8_t n_base = 0;

    for (uint8_t i = 0; i < arp->n_held && n_base < LOOM_ARP_MAX_SEQ; i++) {
        base[n_base++] = arp->held[i];
    }

    /* Merge chord progression notes if enabled */
    if (arp->chord_prog_enabled && arp->n_held > 0) {
        uint16_t steps_per_chord = arp->latched ? 32 : 8;
        uint16_t harmonic_step = (arp->step / steps_per_chord) % LOOM_CHORD_STEPS;
        const loom_chord_t *ch = &arp->prog.chords[harmonic_step];
        for (uint8_t c = 0; c < ch->count; c++) {
            uint8_t cn = ch->notes[c];
            bool found = false;
            for (uint8_t b = 0; b < n_base; b++) {
                if (base[b] == cn) {
                    found = true;
                    break;
                }
            }
            if (!found && n_base < LOOM_ARP_MAX_SEQ) {
                base[n_base++] = cn;
            }
        }
    }

    /* Expand octaves */
    uint8_t expanded[LOOM_ARP_MAX_SEQ];
    uint8_t exp_len = 0;
    uint8_t octs = arp->octaves;
    if (octs > 3) octs = 3;

    for (uint8_t o = 0; o <= octs; o++) {
        for (uint8_t i = 0; i < n_base && exp_len < LOOM_ARP_MAX_SEQ; i++) {
            int16_t n = (int16_t)base[i] + (o * 12);
            if (n <= 127) {
                expanded[exp_len++] = (uint8_t)n;
            }
        }
    }

    if (exp_len == 0) return;

    /* Sort and deduplicate expanded pool */
    loom_arp_sort(expanded, exp_len);
    uint8_t uniq_len = 0;
    for (uint8_t i = 0; i < exp_len; i++) {
        if (i == 0 || expanded[i] != expanded[i - 1]) {
            expanded[uniq_len++] = expanded[i];
        }
    }
    exp_len = uniq_len;

    /* Build sequence according to mode */
    switch (arp->mode) {
    case LOOM_ARP_UP:
        for (uint8_t i = 0; i < exp_len && i < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[arp->seq_len++] = expanded[i];
        }
        break;

    case LOOM_ARP_DOWN:
        for (int i = (int)exp_len - 1; i >= 0 && arp->seq_len < LOOM_ARP_MAX_SEQ; i--) {
            arp->sequence[arp->seq_len++] = expanded[i];
        }
        break;

    case LOOM_ARP_UP_DOWN:
        for (uint8_t i = 0; i < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[arp->seq_len++] = expanded[i];
        }
        for (int i = (int)exp_len - 2; i > 0 && arp->seq_len < LOOM_ARP_MAX_SEQ; i--) {
            arp->sequence[arp->seq_len++] = expanded[i];
        }
        break;

    case LOOM_ARP_STAGGER_UP:
        for (uint8_t i = 0; i < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[arp->seq_len++] = expanded[i];
            if (i + 2 < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ) {
                arp->sequence[arp->seq_len++] = expanded[i + 2];
            }
        }
        break;

    case LOOM_ARP_STAGGER_DOWN: {
        uint8_t rev[LOOM_ARP_MAX_SEQ];
        for (uint8_t i = 0; i < exp_len; i++) {
            rev[i] = expanded[exp_len - 1 - i];
        }
        for (uint8_t i = 0; i < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[arp->seq_len++] = rev[i];
            if (i + 2 < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ) {
                arp->sequence[arp->seq_len++] = rev[i + 2];
            }
        }
        break;
    }

    case LOOM_ARP_RANDOM:
        for (uint8_t i = 0; i < exp_len && i < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[i] = expanded[i];
        }
        arp->seq_len = exp_len;
        /* Fisher-Yates shuffle */
        for (int i = (int)arp->seq_len - 1; i > 0; i--) {
            uint32_t j = loom_arp_rand(arp) % (i + 1);
            uint8_t tmp = arp->sequence[i];
            arp->sequence[i] = arp->sequence[j];
            arp->sequence[j] = tmp;
        }
        break;

    case LOOM_ARP_BACH: {
        /* 3 forward, 1 back */
        int count = exp_len + 4;
        for (int i = 0; i < count && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            int base_shift = i / 3;
            int internal_step = i % 3;
            int idx = (base_shift + internal_step) % exp_len;
            arp->sequence[arp->seq_len++] = expanded[idx];
        }
        break;
    }

    case LOOM_ARP_BROWNIAN: {
        int cur = 0;
        for (uint8_t i = 0; i < 32 && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            arp->sequence[arp->seq_len++] = expanded[cur];
            int move = ((int)(loom_arp_rand(arp) % 3)) - 1; /* -1, 0, +1 */
            cur += move;
            if (cur < 0) cur = 0;
            if (cur >= (int)exp_len) cur = exp_len - 1;
        }
        break;
    }

    case LOOM_ARP_CONVERGE: {
        for (uint8_t i = 0; i < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            uint8_t offset = i / 2;
            uint8_t idx = (i % 2 == 0) ? offset : (exp_len - 1 - offset);
            if (idx < exp_len) {
                arp->sequence[arp->seq_len++] = expanded[idx];
            }
        }
        break;
    }

    case LOOM_ARP_DIVERGE: {
        int center = exp_len / 2;
        for (uint8_t i = 0; i < exp_len && arp->seq_len < LOOM_ARP_MAX_SEQ; i++) {
            int offset = (i + 1) / 2;
            int idx = center + (offset * ((i % 2 == 1) ? -1 : 1));
            if (idx >= 0 && idx < (int)exp_len) {
                arp->sequence[arp->seq_len++] = expanded[idx];
            }
        }
        break;
    }

    default:
        break;
    }
}

void loom_arp_init(loom_arp_t *arp)
{
    arp->mode = LOOM_ARP_UP;
    arp->octaves = 0;
    arp->inversion = 0;
    arp->gate_len = 64;
    arp->prob = 128; /* 100% */
    arp->weird = 0;
    arp->latched = false;
    arp->waiting_for_gesture = false;

    /* Initialize rhythm lanes: Lane 3 (Root) all 1s (0xFFFF), others 0 */
    for (uint8_t i = 0; i < LOOM_ARP_LANES; i++) {
        arp->rhythms[i] = (i == 3) ? 0xFFFF : 0x0000;
    }

    arp->step = 0;
    arp->note_idx = 0;
    arp->rng_state = 0x12345678;

    arp->rate_mult = 1;
    arp->speed_mult_q8 = 256;
    arp->has_drops = false;
    arp->is_weird = false;

    arp->n_held = 0;
    arp->seq_len = 0;

    arp->chord_prog_enabled = false;
    arp->chord_mood = LOOM_MOOD_CALM;
    arp->chord_complexity = LOOM_COMPLEXITY_SIMPLE;
    arp->scale_root = 48; /* C3 */
    arp->scale_len = 7;
    static const uint8_t maj_scale[7] = {0, 2, 4, 5, 7, 9, 11};
    for (uint8_t i = 0; i < 7; i++) {
        arp->scale_intervals[i] = maj_scale[i];
    }
    arp->last_harmonic_step = -1;
}

void loom_arp_set_mode(loom_arp_t *arp, loom_arp_mode_t mode)
{
    if (arp->mode != mode) {
        arp->mode = mode;
        arp->step = 0;
        arp->note_idx = 0;
        loom_arp_update_sequence(arp);
    }
}

void loom_arp_set_octaves(loom_arp_t *arp, uint8_t octaves)
{
    arp->octaves = octaves;
    loom_arp_update_sequence(arp);
}

void loom_arp_set_latched(loom_arp_t *arp, bool latched)
{
    arp->latched = latched;
    if (!latched) {
        arp->n_held = 0;
        arp->seq_len = 0;
        arp->waiting_for_gesture = false;
    }
}

void loom_arp_note_on(loom_arp_t *arp, uint8_t note)
{
    if (arp->latched && arp->waiting_for_gesture) {
        arp->n_held = 0;
        arp->waiting_for_gesture = false;
    }

    bool was_empty = (arp->n_held == 0);

    /* Check if already present */
    for (uint8_t i = 0; i < arp->n_held; i++) {
        if (arp->held[i] == note) return;
    }

    if (arp->n_held < LOOM_ARP_MAX_HELD) {
        arp->held[arp->n_held++] = note;
        loom_arp_sort(arp->held, arp->n_held);

        loom_arp_generate_prog(arp);
        loom_arp_update_sequence(arp);

        if (was_empty) {
            arp->step = 0;
            arp->note_idx = 0;
            if (!arp->latched) {
                loom_arp_roll_weird(arp);
            } else {
                arp->rate_mult = 1;
                arp->speed_mult_q8 = 256;
                arp->is_weird = false;
            }
        }
    }
}

void loom_arp_note_off(loom_arp_t *arp, uint8_t note)
{
    if (arp->latched) {
        return; /* Keep notes until next gesture */
    }

    for (uint8_t i = 0; i < arp->n_held; i++) {
        if (arp->held[i] == note) {
            for (uint8_t j = i; j + 1 < arp->n_held; j++) {
                arp->held[j] = arp->held[j + 1];
            }
            arp->n_held--;
            loom_arp_generate_prog(arp);
            loom_arp_update_sequence(arp);
            break;
        }
    }
}

void loom_arp_all_released(loom_arp_t *arp)
{
    if (arp->latched) {
        arp->waiting_for_gesture = true;
    } else {
        arp->n_held = 0;
        arp->rate_mult = 1;
        arp->speed_mult_q8 = 256;
        arp->step = 0;
        arp->note_idx = 0;
        arp->is_weird = false;
        loom_arp_update_sequence(arp);
    }
}

void loom_arp_clear(loom_arp_t *arp)
{
    arp->n_held = 0;
    arp->seq_len = 0;
    arp->step = 0;
    arp->note_idx = 0;
    arp->last_harmonic_step = -1;
    arp->waiting_for_gesture = false;
}

void loom_arp_set_chord_config(loom_arp_t *arp, bool enabled, uint8_t mood, uint8_t complexity)
{
    arp->chord_prog_enabled = enabled;
    arp->chord_mood = mood;
    arp->chord_complexity = complexity;
    loom_arp_generate_prog(arp);
    loom_arp_update_sequence(arp);
}

void loom_arp_set_scale_config(loom_arp_t *arp, uint8_t root, const uint8_t *scale, uint8_t len)
{
    arp->scale_root = root;
    arp->scale_len = (len > 12) ? 12 : len;
    for (uint8_t i = 0; i < arp->scale_len; i++) {
        arp->scale_intervals[i] = scale[i];
    }
    loom_arp_generate_prog(arp);
    loom_arp_update_sequence(arp);
}

void loom_arp_toggle_rhythm_step(loom_arp_t *arp, uint8_t lane, uint8_t step)
{
    if (lane < LOOM_ARP_LANES && step < LOOM_ARP_RHYTHM_STEPS) {
        arp->rhythms[lane] ^= (uint16_t)(1u << step);
    }
}

uint8_t loom_arp_tick(loom_arp_t *arp, uint8_t *out_notes, uint8_t max_out)
{
    if (arp->seq_len == 0 || arp->mode == LOOM_ARP_OFF || max_out == 0) {
        return 0;
    }

    /* Harmonic step update */
    if (arp->chord_prog_enabled && arp->n_held > 0) {
        uint16_t steps_per_chord = arp->latched ? 32 : 8;
        int16_t harmonic_step = (arp->step / steps_per_chord) % LOOM_CHORD_STEPS;
        if (harmonic_step != arp->last_harmonic_step) {
            arp->last_harmonic_step = harmonic_step;
            loom_arp_update_sequence(arp);
            if (arp->seq_len == 0) return 0;
        }
    }

    uint16_t max_steps = arp->latched ? 16 : 8;
    uint16_t step_idx = arp->step % max_steps;

    uint8_t out_count = 0;
    bool played_any = false;

    /* Probability check for this step */
    bool step_allowed = true;
    if (arp->prob < 128) {
        step_allowed = ((loom_arp_rand(arp) & 127) <= arp->prob);
    }

    uint8_t seq_len = arp->seq_len;

    /* Lane 3: Root Note */
    if ((arp->rhythms[3] & (1u << step_idx)) != 0) {
        if (step_allowed && out_count < max_out) {
            uint8_t idx = arp->note_idx % seq_len;
            int16_t note = arp->sequence[idx];
            if (arp->inversion != 0 && (arp->note_idx % seq_len) == 0) {
                note += arp->inversion * 12;
            }
            if (note > 127) note = 127;
            out_notes[out_count++] = (uint8_t)note;
        }
        played_any = true;
    }

    /* Helper macro for walk lanes */
    #define CHECK_LANE(lane_idx, offset_expr) do { \
        if ((arp->rhythms[lane_idx] & (1u << step_idx)) != 0) { \
            if (step_allowed && out_count < max_out) { \
                uint8_t idx = (offset_expr) % seq_len; \
                out_notes[out_count++] = arp->sequence[idx]; \
            } \
            played_any = true; \
        } \
    } while (0)

    CHECK_LANE(2, arp->note_idx + 1);
    CHECK_LANE(1, arp->note_idx + 2);
    CHECK_LANE(0, arp->note_idx + 3);
    CHECK_LANE(4, (uint16_t)(arp->note_idx + seq_len - 1));
    CHECK_LANE(5, (uint16_t)(arp->note_idx + seq_len - (2 % seq_len)));
    CHECK_LANE(6, (uint16_t)(arp->note_idx + seq_len - (3 % seq_len)));

    #undef CHECK_LANE

    if (played_any) {
        arp->note_idx++;
    }
    arp->step++;

    return out_count;
}

const char *loom_arp_mode_name(loom_arp_mode_t mode)
{
    static const char *const names[LOOM_ARP_MODE_COUNT] = {
        "OFF", "UP", "DOWN", "UPDN", "STAG-UP", "STAG-DN",
        "RANDOM", "BACH", "BROWNIAN", "CONVERGE", "DIVERGE"
    };
    if (mode < LOOM_ARP_MODE_COUNT) {
        return names[mode];
    }
    return "UNKNOWN";
}
