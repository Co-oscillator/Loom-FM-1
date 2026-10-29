/* SPDX-License-Identifier: GPL-3.0-only
 * Standalone test for Loom Chord Progression Engine */
#include <stdio.h>
#include <assert.h>
#include "../firmware/src/loom_chord.h"

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void print_note(uint8_t note) {
    int oct = (note / 12) - 1;
    printf("%s%d(%d)", NOTE_NAMES[note % 12], oct, note);
}

static void print_progression(const loom_chord_prog_t *prog) {
    printf("Progression [Root: ");
    print_note(prog->root_note);
    printf(" | Mood: %s | Complexity: %s]:\n",
           loom_chord_mood_name((loom_mood_t)prog->mood),
           loom_chord_complexity_name((loom_complexity_t)prog->complexity));

    for (int i = 0; i < LOOM_CHORD_STEPS; i++) {
        const loom_chord_t *c = &prog->chords[i];
        printf("  Step %d [Deg %d]: ", i + 1, c->degree);
        for (int j = 0; j < c->count; j++) {
            print_note(c->notes[j]);
            if (j + 1 < c->count) printf(" ");
        }
        printf("\n");
    }
    printf("\n");
}

int main(void) {
    printf("=== Testing Loom Chord Progression Engine ===\n\n");

    /* C Major scale */
    static const uint8_t C_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
    /* A Minor scale */
    static const uint8_t A_MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

    uint32_t rng = 123456789;
    loom_chord_prog_t prog;

    /* Test 1: Simple Triads in C Major, Calm mood */
    loom_chord_generate(&prog, 60, C_MAJOR, 7, LOOM_MOOD_CALM, LOOM_COMPLEXITY_SIMPLE, LOOM_VOICE_CLOSE, NULL, 0, &rng);
    print_progression(&prog);
    for (int i = 0; i < LOOM_CHORD_STEPS; i++) {
        assert(prog.chords[i].count == 3);
    }

    /* Test 2: Complex (7th/9th) in A Minor, Sad mood */
    loom_chord_generate(&prog, 57, A_MINOR, 7, LOOM_MOOD_SAD, LOOM_COMPLEXITY_COMPLEX, LOOM_VOICE_CLOSE, NULL, 0, &rng);
    print_progression(&prog);
    for (int i = 0; i < LOOM_CHORD_STEPS; i++) {
        assert(prog.chords[i].count == 5);
    }

    /* Test 3: Coltrane Changes in C Major, Happy mood */
    loom_chord_generate(&prog, 60, C_MAJOR, 7, LOOM_MOOD_HAPPY, LOOM_COMPLEXITY_COLTRANE, LOOM_VOICE_CLOSE, NULL, 0, &rng);
    print_progression(&prog);
    for (int i = 0; i < LOOM_CHORD_STEPS; i++) {
        assert(prog.chords[i].count == 5);
    }

    /* Test 4: Verify All 8 Moods Generate Cleanly */
    for (int m = 0; m < LOOM_MOOD_COUNT; m++) {
        loom_chord_generate(&prog, 60, C_MAJOR, 7, (loom_mood_t)m, LOOM_COMPLEXITY_SIMPLE, LOOM_VOICE_CLOSE, NULL, 0, &rng);
        assert(prog.chords[0].count == 3);
        assert(prog.chords[7].count == 3);
    }

    /* Test 5: every voicing, on the same progression (VOIC while CHRD = PROG) */
    {
        loom_chord_prog_t base, v;
        loom_voicing_t vo;
        uint32_t seed = 424242u;
        int i, j, k, ok = 1;

        rng = seed;
        loom_chord_generate(&base, 60, C_MAJOR, 7, LOOM_MOOD_CALM, LOOM_COMPLEXITY_SIMPLE,
                            LOOM_VOICE_CLOSE, NULL, 0, &rng);
        for (i = 0; i < LOOM_CHORD_STEPS; i++)
            ok &= base.chords[i].count == 3;
        for (vo = LOOM_VOICE_OPEN; vo < LOOM_VOICE_COUNT; vo++) {
            rng = seed;
            loom_chord_generate(&v, 60, C_MAJOR, 7, LOOM_MOOD_CALM, LOOM_COMPLEXITY_SIMPLE,
                                vo, NULL, 0, &rng);
            for (i = 0; i < LOOM_CHORD_STEPS; i++) {
                const loom_chord_t *a = &base.chords[i], *b = &v.chords[i];
                uint8_t want = (uint8_t)(vo == LOOM_VOICE_BASS ? 4 : 3);
                ok &= b->count == want && b->count <= LOOM_CHORD_MAX_NOTES;
                for (j = 1; j < b->count; j++)
                    ok &= b->notes[j] > b->notes[j - 1];
                /* the same pitch classes as CLOSE (BASS repeats the root an octave down) */
                for (j = 0; j < b->count; j++) {
                    int found = 0;
                    for (k = 0; k < a->count; k++)
                        found |= (a->notes[k] % 12) == (b->notes[j] % 12);
                    ok &= found;
                }
                if (vo == LOOM_VOICE_BASS)
                    ok &= (int)b->notes[0] + 12 == (int)b->notes[1];
            }
        }
        if (!ok)
            printf("  a voicing changed the progression's notes or its shape\n");
        assert(ok && "every voicing of the same progression: 3 (BASS: 4) ascending notes, the same pitch classes");
        printf("  VOIC of a generated progression: CLOSE / OPEN / INV1 / INV2 / +OCT (OK)\n");
    }

    printf("ALL CHORD PROGRESSION TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
