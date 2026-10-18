/* SPDX-License-Identifier: GPL-3.0-only
 * Standalone test for Loom Arpeggiator */
#include <stdio.h>
#include <assert.h>
#include "../firmware/src/loom_chord.h"
#include "../firmware/src/loom_arp.h"

/* Forward declare or include implementation */
#include "../firmware/src/loom_chord.c"
#include "../firmware/src/loom_arp.c"

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void print_notes(const uint8_t *notes, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        uint8_t n = notes[i];
        int oct = (n / 12) - 1;
        printf("%s%d(%d)", NOTE_NAMES[n % 12], oct, n);
        if (i + 1 < count) printf(" ");
    }
}

int main(void)
{
    printf("=== Testing Loom Arpeggiator ===\n\n");

    loom_arp_t arp;
    loom_arp_init(&arp);

    /* Test 1: Basic UP mode with C Major Triad (C4=60, E4=64, G4=67) */
    printf("Test 1: Basic UP mode...\n");
    loom_arp_note_on(&arp, 60);
    loom_arp_note_on(&arp, 64);
    loom_arp_note_on(&arp, 67);

    assert(arp.n_held == 3);
    assert(arp.seq_len == 3);
    assert(arp.sequence[0] == 60);
    assert(arp.sequence[1] == 64);
    assert(arp.sequence[2] == 67);

    uint8_t out[8];
    uint8_t n = loom_arp_tick(&arp, out, 8);
    assert(n == 1 && out[0] == 60);
    n = loom_arp_tick(&arp, out, 8);
    assert(n == 1 && out[0] == 64);
    n = loom_arp_tick(&arp, out, 8);
    assert(n == 1 && out[0] == 67);
    n = loom_arp_tick(&arp, out, 8);
    assert(n == 1 && out[0] == 60); /* loops */
    printf("  UP mode OK.\n");

    /* Test 2: DOWN mode */
    printf("Test 2: DOWN mode...\n");
    loom_arp_set_mode(&arp, LOOM_ARP_DOWN);
    assert(arp.seq_len == 3);
    assert(arp.sequence[0] == 67);
    assert(arp.sequence[1] == 64);
    assert(arp.sequence[2] == 60);
    printf("  DOWN mode OK.\n");

    /* Test 3: UP_DOWN mode */
    printf("Test 3: UP_DOWN mode...\n");
    loom_arp_set_mode(&arp, LOOM_ARP_UP_DOWN);
    /* 60, 64, 67, 64 */
    assert(arp.seq_len == 4);
    assert(arp.sequence[0] == 60);
    assert(arp.sequence[1] == 64);
    assert(arp.sequence[2] == 67);
    assert(arp.sequence[3] == 64);
    printf("  UP_DOWN mode OK.\n");

    /* Test 4: Octave expansion */
    printf("Test 4: Octaves...\n");
    loom_arp_set_mode(&arp, LOOM_ARP_UP);
    loom_arp_set_octaves(&arp, 1); /* 2 octaves total */
    assert(arp.seq_len == 6);
    assert(arp.sequence[0] == 60);
    assert(arp.sequence[1] == 64);
    assert(arp.sequence[2] == 67);
    assert(arp.sequence[3] == 72);
    assert(arp.sequence[4] == 76);
    assert(arp.sequence[5] == 79);
    printf("  Octaves OK.\n");

    /* Test 5: BACH mode */
    printf("Test 5: BACH mode...\n");
    loom_arp_set_octaves(&arp, 0);
    loom_arp_set_mode(&arp, LOOM_ARP_BACH);
    printf("  BACH seq (len %d): ", arp.seq_len);
    print_notes(arp.sequence, arp.seq_len);
    printf("\n");
    assert(arp.seq_len > 3);
    printf("  BACH mode OK.\n");

    /* Test 6: CONVERGE and DIVERGE modes */
    printf("Test 6: CONVERGE / DIVERGE modes...\n");
    loom_arp_set_mode(&arp, LOOM_ARP_CONVERGE);
    /* Converge: 0 (60), 2 (67), 1 (64) */
    assert(arp.sequence[0] == 60);
    assert(arp.sequence[1] == 67);
    assert(arp.sequence[2] == 64);

    loom_arp_set_mode(&arp, LOOM_ARP_DIVERGE);
    assert(arp.seq_len == 3);
    printf("  CONVERGE/DIVERGE OK.\n");

    /* Test 7: Rhythm Lanes */
    printf("Test 7: Rhythm Lanes...\n");
    loom_arp_set_mode(&arp, LOOM_ARP_UP);
    /* Enable Lane 2 (+1 walk) on step 0 */
    arp.rhythms[2] = 0x0001; /* active on step 0 */
    arp.step = 0;
    arp.note_idx = 0;
    n = loom_arp_tick(&arp, out, 8);
    /* Both lane 3 (root=60) and lane 2 (+1=64) should fire! */
    assert(n == 2);
    assert(out[0] == 60);
    assert(out[1] == 64);
    printf("  Rhythm lanes multi-note OK.\n");

    /* Test 8: Latch and Release */
    printf("Test 8: Latched behavior...\n");
    loom_arp_set_latched(&arp, true);
    loom_arp_all_released(&arp);
    /* In latch mode, held notes should persist! */
    assert(arp.n_held == 3);
    assert(arp.waiting_for_gesture == true);

    /* Pressing new note replaces latched gesture */
    loom_arp_note_on(&arp, 72);
    assert(arp.n_held == 1);
    assert(arp.held[0] == 72);
    assert(arp.waiting_for_gesture == false);
    printf("  Latched gesture OK.\n");

    /* Test 9: Chord Progression Coupling */
    printf("Test 9: Chord progression coupling...\n");
    loom_arp_set_latched(&arp, false);
    loom_arp_clear(&arp);
    loom_arp_note_on(&arp, 60); /* C4 held */
    loom_arp_set_chord_config(&arp, true, LOOM_MOOD_HAPPY, LOOM_COMPLEXITY_SIMPLE);

    /* Chord prog should be generated and merged into sequence */
    printf("  Sequence with chord prog merged: len=%d: ", arp.seq_len);
    print_notes(arp.sequence, arp.seq_len);
    printf("\n");
    assert(arp.seq_len >= 3); /* Triad generated from root */
    printf("  Chord progression coupling OK.\n");

    /* Test 10: Weirdness logic */
    printf("Test 10: Weirdness variation...\n");
    arp.weird = 100; /* High weirdness */
    loom_arp_clear(&arp);
    loom_arp_note_on(&arp, 60);
    printf("  Weirdness active: rate_mult=%d, speed_mult_q8=%d\n",
           arp.rate_mult, arp.speed_mult_q8);
    assert(arp.rate_mult >= 1);
    assert(arp.speed_mult_q8 > 0);
    printf("  Weirdness OK.\n");

    printf("\nALL ARPEGGIATOR TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
