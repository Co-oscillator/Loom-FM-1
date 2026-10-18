/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Loom Pi / Loom FM-1 Contributors */
#ifndef LOOM_ARP_H
#define LOOM_ARP_H

#include <stdint.h>
#include <stdbool.h>
#include "loom_chord.h"

#define LOOM_ARP_MAX_HELD 16
#define LOOM_ARP_MAX_SEQ 64
#define LOOM_ARP_LANES 7
#define LOOM_ARP_RHYTHM_STEPS 16

typedef enum {
    LOOM_ARP_OFF = 0,
    LOOM_ARP_UP = 1,
    LOOM_ARP_DOWN = 2,
    LOOM_ARP_UP_DOWN = 3,
    LOOM_ARP_STAGGER_UP = 4,
    LOOM_ARP_STAGGER_DOWN = 5,
    LOOM_ARP_RANDOM = 6,
    LOOM_ARP_BACH = 7,
    LOOM_ARP_BROWNIAN = 8,
    LOOM_ARP_CONVERGE = 9,
    LOOM_ARP_DIVERGE = 10,
    LOOM_ARP_MODE_COUNT = 11
} loom_arp_mode_t;

typedef struct {
    loom_arp_mode_t mode;
    uint8_t octaves;          /* 0 = 1 oct, 1 = 2 oct, 2 = 3 oct */
    uint8_t inversion;
    uint8_t gate_len;         /* 0..128 (default 64) */
    uint8_t prob;             /* 0..128 probability (128 = 100%) */
    uint8_t weird;            /* 0..128 weirdness amount */
    bool latched;
    bool waiting_for_gesture;

    /* Rhythm lanes: 7 lanes x 16 steps bitmask. (Bit s in lane l: 1 = play) */
    uint16_t rhythms[LOOM_ARP_LANES]; /* default: lane 3 = 0xFFFF, others 0 */

    /* Step indices */
    uint16_t step;
    uint16_t note_idx;
    uint32_t rng_state;

    /* Dynamic gesture modifiers from Weirdness */
    uint8_t rate_mult;        /* 1, 2, 4, 7 */
    uint16_t speed_mult_q8;   /* 1.0 = 256 (Q8 fixed-point) */
    bool has_drops;
    bool is_weird;

    /* Notes physically held */
    uint8_t n_held;
    uint8_t held[LOOM_ARP_MAX_HELD];

    /* Expanded Sequence */
    uint8_t seq_len;
    uint8_t sequence[LOOM_ARP_MAX_SEQ];

    /* Chord Progression coupling */
    bool chord_prog_enabled;
    uint8_t chord_mood;
    uint8_t chord_complexity;
    uint8_t scale_root;
    uint8_t scale_len;
    uint8_t scale_intervals[12];
    loom_chord_prog_t prog;
    int16_t last_harmonic_step;
} loom_arp_t;

void loom_arp_init(loom_arp_t *arp);
void loom_arp_set_mode(loom_arp_t *arp, loom_arp_mode_t mode);
void loom_arp_set_octaves(loom_arp_t *arp, uint8_t octaves);
void loom_arp_set_latched(loom_arp_t *arp, bool latched);
void loom_arp_note_on(loom_arp_t *arp, uint8_t note);
void loom_arp_note_off(loom_arp_t *arp, uint8_t note);
void loom_arp_all_released(loom_arp_t *arp);
void loom_arp_clear(loom_arp_t *arp);

/* Configuration */
void loom_arp_set_chord_config(loom_arp_t *arp, bool enabled, uint8_t mood, uint8_t complexity);
void loom_arp_set_scale_config(loom_arp_t *arp, uint8_t root, const uint8_t *scale, uint8_t len);

/* Rhythm step toggling (e.g. from the 16 keys) */
void loom_arp_toggle_rhythm_step(loom_arp_t *arp, uint8_t lane, uint8_t step);

/* Advance step & retrieve notes to play (returns number of notes, up to max_out in out_notes) */
uint8_t loom_arp_tick(loom_arp_t *arp, uint8_t *out_notes, uint8_t max_out);

const char *loom_arp_mode_name(loom_arp_mode_t mode);

#endif /* LOOM_ARP_H */
