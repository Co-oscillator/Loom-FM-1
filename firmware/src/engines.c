/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SAVE > PHRASES) and the parts' sounds at power-on. */
#include "dsp.c"

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with large per-part state
 * (engine_t.mem: PHYS's slots, GRAIN's grains and index, DRUM's lanes, FM6's notes, SLICE's reverse windows)
 * keep it in one region per part instead of an array each. The region holds the state of the engine that
 * last claimed it (eng_mem_owner); a switch to another such engine zeroes its engine_t.mem bytes, which is
 * the state each had at power-on, so a switch sounds as it did with the arrays (tests/union_test.c). State
 * that runs on across notes and switches stays outside (WHEEL's wheels, GRAIN's random numbers).
 * Sized by the largest, PHYS (3 x SYMP's slot); an engine that needs more than ENG_MEM_PART grows it for
 * every part (x NPART of the pool). Zero-initialised pool section: main.c fm1_cstart clears it. */
#define ENG_MEM_PART 12888u
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
#define ENG_MEM(p) ((void *)eng_mem[p].b)  /* part p's region (p < NPART) */
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")

#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_grain.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if FELUCCA_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; FELUCCA_FM4=1 builds it) */
#endif
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#include "eng_scrub.c"
#include "eng_wavetable.c"

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 */
#if FELUCCA_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
    &ENG_PHASE,                  /* 2 */
    &ENG_LOFI,                   /* 3 */
    &ENG_SAMPLE,                 /* 4 */
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_SCRUB,                  /* 6 (ENGI_SCRUB: Tape & Platter Scrub) */
    &ENG_WHEEL,                  /* 7 */
    &ENG_GRAIN,                  /* 8 */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if FELUCCA_SLICE
    &ENG_SLICE,                  /* 13 (FELUCCA_SLICE=0 builds without it) */
#endif
    &ENG_WAVETABLE,              /* 14 (ENGI_WAVETABLE) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* Loom Neon Engine Theme Colors (RGB565) */
typedef struct {
    uint16_t theme;   /* primary neon identity */
    uint16_t accent;  /* hot knob / active cursor accent */
} eng_neon_t;

static const eng_neon_t ENG_NEON[NENGINES] = {
    [0]  = { 0x3FE2, 0x07FF }, /* ANALOG:   Neon Lime (#39FF14), Accent Cyan (#00FFFF) */
#if FELUCCA_FM4
    [1]  = { 0x07FF, 0xF80F }, /* DIGITAL:  Neon Cyan (#00FFFF), Accent Magenta (#FF007F) */
#else
    [1]  = { 0x07FF, 0xF80F }, /* GONE:     Neon Cyan */
#endif
    [2]  = { 0x073F, 0x3FE2 }, /* PHASE:    Electric Aqua (#00E5FF), Accent Lime (#39FF14) */
    [3]  = { 0xFCC0, 0x07FF }, /* LOFI:     Neon Amber (#FF9900), Accent Cyan (#00FFFF) */
    [4]  = { 0xF80F, 0x07FF }, /* SAMPLE:   Neon Pink (#FF007F), Accent Cyan (#00FFFF) */
    [5]  = { 0xF8B2, 0xFEE0 }, /* VOICE:    Neon Coral Pink (#FF1493), Accent Gold (#FFD700) */
    [6]  = { 0xF80F, 0x07FF }, /* SCRUB:    Neon Hot Pink (#FF007F), Accent Cyan (#00FFFF) */
    [7]  = { 0xFD40, 0x1C9F }, /* WHEEL:    Warm Amber (#FFAA00), Accent Blue (#1E90FF) */
    [8]  = { 0xB81F, 0x07FF }, /* GRAIN:    Neon Purple (#BD00FF), Accent Cyan (#00FFFF) */
    [9]  = { 0x1C9F, 0xFEE0 }, /* PHYS:     Electric Blue (#1E90FF), Accent Gold (#FFD700) */
    [10] = { 0xFEE0, 0xFA20 }, /* DRUM:     Neon Gold (#FFD700), Accent Orange-Red (#FF4500) */
    [11] = { 0x87FF, 0xF80F }, /* NOISE:    Ice Cyan (#80FFFF), Accent Hot Pink (#FF007F) */
    [12] = { 0x07FF, 0xF80F }, /* FM6:      Neon Cyan (#00FFFF), Accent Magenta (#FF007F) */
#if FELUCCA_SLICE
    [13] = { 0xF80F, 0x3FE2 }, /* SLICE:    Neon Hot Pink (#FF007F), Accent Lime (#39FF14) */
#endif
    [14] = { 0x07FF, 0x3FE2 }, /* WAVETABLE: Electric Cyan (#00FFFF), Accent Lime (#39FF14) */
};

static inline uint16_t eng_theme_color(uint32_t eng) { return ENG_NEON[eng_idx(eng)].theme; }
static inline uint16_t eng_accent_color(uint32_t eng) { return ENG_NEON[eng_idx(eng)].accent; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* the sample source a track's sound plays that has no data (SAMPLE's SET, GRAIN's SRC, SLICE's SRC: an empty or
 * invalid user slot, a set or PIANO this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
#if FELUCCA_SLICE
    if (e == &ENG_SLICE) {
        si = slc_src_of(t->p);
        *code = 64u + si;
        return slc_get(si) ? 0 : N_SLC_SRC[si];
    }
#endif
    if (e == &ENG_SCRUB) {
        si = (uint32_t)clamp(t->p[P_E4], 0, SLC_NSRC - 1);
        *code = 96u + si;
        return slc_get(si) ? 0 : N_SLC_SRC[si];
    }
    if (e == &ENG_WAVETABLE) {
        uint32_t wsrc = (uint32_t)clamp(t->p[P_E0], 0, WT_NSRC - 1u);
        if (wsrc >= 5 && wsrc <= 7) {
            uint32_t slot = wsrc - 5u;
            *code = 128u + slot;
            return usr_nz[slot] ? 0 : N_WT_SRC[wsrc];
        }
        return 0;
    }
    if (e != &ENG_SAMPLE && e != &ENG_GRAIN)
        return 0;
    *code = (e == &ENG_GRAIN ? 32u : 1u) + si % 32u;
    return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with FELUCCA_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
#if FELUCCA_FM4
    1,                           /* DIGITAL */
#endif
    2, 3,
    14,                          /* WAVETABLE */
    4,
#if FELUCCA_SLICE
    13,                          /* SLICE */
#endif
    6,                           /* SCRUB (Tape & Platter) */
    8,                           /* GRAIN */
    5, 7, 9,                     /* VOICE WHEEL PHYS */
    11,                          /* NOISE */
    10,                          /* DRUM */
};

/* the engines one can pick (engine 1 only with FELUCCA_FM4), in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps).
 * Track 5 (song.sel >= NPART) is strictly DRUM; Tracks 1-4 are strictly Melodic (no DRUM). */
static int eng_ok(uint32_t e)
{
    if (song.sel >= NPART)
        return e == ENGI_DRUM;
    return e < NENGINES && (FELUCCA_FM4 || e != ENGI_DIGITAL) && e != ENGI_DRUM;
}

static uint32_t eng_vis(uint32_t n)
{
    if (song.sel >= NPART)
        return ENGI_DRUM;
    return ENGINE_ORDER[n % (NENG_SHOWN - 1u)];
}

static uint32_t eng_rank(uint32_t e)
{
    if (song.sel >= NPART)
        return 0;
    if (!eng_ok(e))
        e = ENGI_ANALOG;
    uint32_t n;
    for (n = 0; n < NENG_SHOWN - 1u && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN - 1u ? n : 0u;
}

static uint32_t eng_step(uint32_t e, int32_t dir)
{
    if (song.sel >= NPART)
        return ENGI_DRUM;
    uint32_t count = NENG_SHOWN - 1u;
    return ENGINE_ORDER[(eng_rank(e % NENGINES) + (dir > 0 ? 1u : count - 1u)) % count];
}

/* factory sequence patterns: SAVE > PHRASES loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (DRUM plays them as GM drums, SLICE as slices; SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* SLICE (eng_slice.c): note = C4 + slice */
    {"CHOP", {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64},     /* 9 16 slices re-ordered */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"STUTTER", {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67},    /* 10 8 slices, repeats */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"SLICES", {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},   /* 11 in order */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* DRUM (General MIDI: 36 kick, 38 snare, 42 closed / 46 open hi-hat) */
    {"BEAT", {36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},     /* 12 */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP presets (RAVE, ARP 8BIT, ARP LEAD) suggest
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 13 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty: all are): T1..T4 melodic + DRUM */
static const uint8_t TRK_DEF[NTRK][3] = {
    {0, 4, 0},         /* T1: SUBTRACTIVE (Acid Bass) */
    {ENGI_FM6, 4, 0},  /* T2: FM6 (FM Pad) */
    {2, 0, 0},         /* T3: WAVETABLE */
    {3, 0, 0},         /* T4: SAMPLER */
    {ENGI_DRUM, 0, 0}  /* DRUM: DEDICATED LOOM DRUM KIT */
};
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NTRK][0]; }
