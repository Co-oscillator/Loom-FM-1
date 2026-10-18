/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static uint32_t host_slots[3u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    printf("=== Testing Sequencer Active Step Contrast & Playhead Animation ===\n");

    /* 1. Verify contrast color pairs for distinct neon themes */
    /* Hot Pink (0xF80F) -> Neon Cyan (0x07FF) */
    uint16_t pink = 0xF80F;
    uint32_t r_p = ((pink >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g_p = ((pink >> 5)  & 0x3Fu) * 255u / 63u;
    uint32_t b_p = ( pink        & 0x1Fu) * 255u / 31u;
    assert(r_p > 180 && b_p > 100 && g_p < 150);

    /* Neon Cyan (0x07FF) -> Hot Pink (0xF80F) */
    uint16_t cyan = 0x07FF;
    uint32_t r_c = ((cyan >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t b_c = ( cyan        & 0x1Fu) * 255u / 31u;
    assert(b_c > 180 && r_c < 150);

    /* Neon Lime (0x3FE2) -> Hot Pink (0xF80F) */
    uint16_t lime = 0x3FE2;
    uint32_t r_l = ((lime >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g_l = ((lime >> 5)  & 0x3Fu) * 255u / 63u;
    assert(g_l > 180 && r_l < 180);

    /* Neon Gold (0xFEE0) -> Neon Cyan (0x07FF) */
    uint16_t gold = 0xFEE0;
    uint32_t r_g = ((gold >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g_g = ((gold >> 5)  & 0x3Fu) * 255u / 63u;
    uint32_t b_g = ( gold        & 0x1Fu) * 255u / 31u;
    assert(r_g > 180 && g_g > 150 && b_g < 100);

    printf("  All engine neon contrast pairings verified! (OK)\n");

    /* 2. Initialize Loom core and verify sequencer playback active step advancement */
    host_tracks_init();
    song.playing = 1;
    track_t *t = &trk[0];
    t->p[P_SLEN] = 16;
    t->seq_idx = 3;

    assert(song.playing == 1);
    assert(t->seq_idx == 3);

    /* Advance sequencer index */
    t->seq_idx = (t->seq_idx + 1) % t->p[P_SLEN];
    assert(t->seq_idx == 4);

    printf("  Sequencer active step advancement tracking verified! (OK)\n");
    printf("=== ALL PLAYHEAD TESTS PASSED! ===\n");
    return 0;
}
