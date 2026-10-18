/* SPDX-License-Identifier: GPL-3.0-only */
/* Verification test for SCRUB engine 4-point cubic Hermite interpolation and rubbery physics */

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static uint32_t host_slots[3u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    printf("=== Testing SCRUB Engine High-Fidelity Interpolation & Physics ===\n");

    host_tracks_init();
    track_t *t = &trk[0];
    host_preset(t, ENGI_SCRUB, 0);

    scrb_part_t *sp = (scrb_part_t *)ENG_MEM(t - trk);
    const slc_src_t *s = slc_get(0); /* BREAK source */
    assert(s != NULL && s->len > 0 && "Default BREAK sample source must be loaded!");
    printf("  Sample source loaded: %u samples @ %u Hz\n", s->len, s->rate);

    /* 1. Monophonic Note On */
    trk_note_on(t, 60, 100);
    assert(t->v[0].active == 1);
    printf("  Voice 0 activated successfully (OK)\n");

    /* 2. Motor Playback at +1.0x */
    t->p[P_E1] = 10; /* +1.0x */
    int32_t buf[2 * CTL];
    double last_pos = sp->pos;
    for (int b = 0; b < 250; b++) {
        mix_block(buf, CTL);
    }
    assert(sp->pos > last_pos && "Position must advance during forward playback!");
    assert(sp->velocity > 0.8 && "Velocity must track +1.0x motor speed!");
    printf("  Forward motor playback at +1.0x: pos=%.1f, vel=%.3f (OK)\n", sp->pos, sp->velocity);

    /* 3. Slow Playback (+0.1x speed): test smooth Hermite interpolation */
    t->p[P_E1] = 1; /* +0.1x speed */
    for (int b = 0; b < 250; b++) {
        mix_block(buf, CTL);
    }
    assert(sp->velocity > 0.05 && sp->velocity < 0.15 && "Velocity must smoothly track +0.1x motor speed!");

    /* Check sample delta continuity across a block */
    mix_block(buf, CTL);
    int32_t max_sample_delta = 0;
    for (int i = 1; i < CTL; i++) {
        int32_t delta = abs(buf[2 * i] - buf[2 * (i - 1)]);
        if (delta > max_sample_delta) max_sample_delta = delta;
    }
    /* With 4-point cubic Hermite interpolation and speed-adaptive lowpass filter,
     * sample-to-sample transitions at 0.1x speed are extremely smooth (no sharp clicks) */
    assert(max_sample_delta < 4000 && "Sample-to-sample deltas must be smooth and continuous!");
    printf("  Slow playback (+0.1x): max inter-sample delta=%d (ultra smooth, no blips) (OK)\n", max_sample_delta);

    /* 4. Motor Reverse Playback at -1.0x */
    t->p[P_E1] = -10; /* -1.0x */
    last_pos = sp->pos;
    for (int b = 0; b < 250; b++) {
        mix_block(buf, CTL);
    }
    assert(sp->pos < last_pos && "Position must decrease during reverse playback!");
    assert(sp->velocity < -0.8 && "Velocity must track -1.0x motor speed!");
    printf("  Reverse motor playback at -1.0x: pos=%.1f, vel=%.3f (OK)\n", sp->pos, sp->velocity);

    /* 5. Rubbery Spring Tracking during Slow Scrubbing */
    t->p[P_E1] = 0; /* Motor STOP */
    for (int b = 0; b < 150; b++) {
        mix_block(buf, CTL);
    }
    /* Motor stopped: velocity decayed to 0 */
    assert(fabs(sp->velocity) < 0.01 && "Motor stop must smoothly bring platter to halt!");
    printf("  Platter friction decay to dead stop (OK)\n");

    /* User turns POS knob slowly forward (from current position ~57% to 70%) */
    printf("  Simulating slow manual knob scrub...\n");
    for (int p_val = 60; p_val <= 70; p_val++) {
        t->p[P_E0] = p_val;
        for (int b = 0; b < 2; b++) mix_block(buf, CTL);
        assert(sp->velocity >= 0.0 && "Forward scrub must produce positive velocity!");
    }
    /* Allow playhead to catch up and settle at 70% */
    for (int b = 0; b < 100; b++) mix_block(buf, CTL);

    /* User turns POS knob slowly backward (from 70% to 50%) */
    for (int p_val = 69; p_val >= 50; p_val--) {
        t->p[P_E0] = p_val;
        for (int b = 0; b < 2; b++) mix_block(buf, CTL);
        assert(sp->velocity <= 0.0 && "Reverse scrub must produce negative velocity!");
    }
    printf("  Reverse scrub spring tracking: vel=%.4f (smooth elastic response) (OK)\n", sp->velocity);

    /* 6. LOOP Toggle and Boundary Handling */
    t->p[P_E5] = 1; /* LOOP ON */
    sp->pos = (double)(s->len - 10);
    t->p[P_E1] = 10; /* Run forward past end of sample */
    for (int b = 0; b < 10; b++) {
        mix_block(buf, CTL);
    }
    assert(sp->pos < (double)s->len && "Position must wrap smoothly within sample range in loop mode!");
    printf("  LOOP wrap-around boundary handling verified (OK)\n");

    printf("\n=== ALL SCRUB HIGH-FIDELITY INTERPOLATION TESTS PASSED! ===\n");
    return 0;
}
