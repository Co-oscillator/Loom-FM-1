/* SPDX-License-Identifier: GPL-3.0-only */
/* Test keyboard chromatic and scale note ordering across engines and presets */

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
    printf("=== Testing Keyboard Chromatic & Scale Note Ordering ===\n");

    host_tracks_init();

    for (uint32_t trk_idx = 0; trk_idx < 4; trk_idx++) {
        track_t *t = &trk[trk_idx];

        /* Verify that CHRD defaults to 0 (OFF), not 1 (PROG) */
        assert(t->p[P_CHRD] == 0 && "P_CHRD must default to 0 (OFF) so keyboard plays notes, not chords!");

        /* Test across multiple engines */
        for (uint32_t eng = 0; eng < NENGINES; eng++) {
            if (!eng_ok(eng) || eng == ENGI_DRUM) continue; /* DRUM maps keys to lanes */

            printf("Testing engine %u: %s\n", eng, ENGINES[eng]->name);
            host_preset(t, eng, 0);
            assert(t->p[P_CHRD] == 0 && "Preset load must maintain P_CHRD = OFF!");

            /* Set to Chromatic (P_SCALE = 0, P_QUANT = 0, P_TRANS = 0, octave = 0) */
            t->p[P_SCALE] = 0;
            t->p[P_QUANT] = 0;
            t->p[P_TRANS] = 0;
            song.octave = 0;

            /* Check that keyboard keys 0..26 produce ascending chromatic notes */
            uint32_t base_note = (strcmp(ENGINES[eng]->name, "SLICE") == 0) ? 60u : 53u;
            for (uint32_t k = 0; k < 27; k++) {
                uint32_t note = kb_map(t, k);
                assert(note == base_note + k && "Keyboard must play strictly consecutive chromatic semitones!");

                uint8_t chord_out[CHORD_MAX];
                uint32_t n = chord_build(t, note, chord_out);
                assert(n == 1 && "CHRD OFF must produce a single note!");
                assert(chord_out[0] == base_note + k && "Played note must match keyboard key note exactly!");
            }
        }
    }

    printf("  All 4 tracks across all melodic engines verified strictly chromatic and in order! (OK)\n");
    printf("=== ALL KEYBOARD TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}
