/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LOOM_DRUM_H
#define LOOM_DRUM_H

#include <stdint.h>

#define LOOM_DRUM_LANES 8

enum {
    LOOM_DRUM_KICK = 0,
    LOOM_DRUM_SNARE = 1,
    LOOM_DRUM_CLAP = 2,
    LOOM_DRUM_HAT_CL = 3,
    LOOM_DRUM_HAT_OP = 4,
    LOOM_DRUM_CYMBAL = 5,
    LOOM_DRUM_PERC = 6,
    LOOM_DRUM_NOISE = 7
};

enum {
    LOOM_KIT_ANALOG = 0,
    LOOM_KIT_FM = 1,
    LOOM_KIT_808 = 2,
    LOOM_KIT_909 = 3,
    LOOM_KIT_606 = 4,
    LOOM_KIT_505 = 5,
    LOOM_KIT_707 = 6,
    LOOM_KIT_COUNT = 7
};

void loom_drum_init(void);
void loom_drum_trigger(uint32_t lane, uint32_t vel, const int16_t *p);
void loom_drum_render(int32_t *out, uint32_t n, uint32_t kit, const int16_t *p);
void loom_drum_render_lane(int32_t *out, uint32_t n, uint32_t kit, const int16_t *p, uint32_t lane);
int  loom_drum_lane_active(uint32_t kit, uint32_t lane);
int  loom_drum_active(uint32_t kit);
void loom_drum_all_off(void);

#endif /* LOOM_DRUM_H */
