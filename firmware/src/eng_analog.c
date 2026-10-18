/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SUBTRACT: 4-oscillator subtractive synthesis engine with smooth waveform morphing,
 * recursive wave folding, drive, mix bypass, and per-oscillator glide and tuning.
 *
 * Page 1 (OSC):
 *   Knob 1: OSC (1, 2, 3, SUB)
 *   Knob 2: WAVE (smoothly morphs: Sine -> Tri -> Saw -> Sqr)
 *   Knob 3: FOLD (0..127 recursive wave folding)
 *   Knob 4: MIX (0..127, where 0 = OFF, completely bypassing the oscillator to save CPU)
 *
 * Page 2 (TUNE):
 *   Knob 1: OSC (1, 2, 3, SUB) [synced with Page 1]
 *   Knob 2: PTCH (-24..+24 semitones; Osc 4 default is -12 ST)
 *   Knob 3: DRV (0..127 per-oscillator analog saturation)
 *   Knob 4: GLID (0..127 per-oscillator independent glide)
 *
 * Page 3 (FILTER):
 *   Knob 1: TYPE (LP, BP, HP)
 *   Knob 2: CUT (Cutoff)
 *   Knob 3: RES (Resonance)
 *   Knob 4: KTR (Key Tracking)
 */

#define SUB_MAGIC 0x53554231u

typedef struct {
    uint8_t wave;    /* 0..127: 0 Sine, 42 Tri, 85 Saw, 127 Sqr */
    uint8_t fold;    /* 0..127 */
    uint8_t mix;     /* 0..127: 0 = OFF */
    int8_t  pitch;   /* -24..+24 semitones */
    uint8_t drive;   /* 0..127 */
    uint8_t glide;   /* 0..127 */
} sub_osc_t;

typedef struct {
    uint32_t magic;
    uint8_t sel_osc;     /* 0..3: currently selected oscillator on EDIT pages */
    uint8_t pad[3];
    sub_osc_t osc[4];

    /* Voice rendering state */
    uint32_t ph[NVOICE][4];
    int32_t  cur_pitch16[NVOICE][4];
} sub_part_t;

ENG_MEM_FITS("SUBTRACT", sizeof(sub_part_t));

static sub_part_t sub_state[NPART];

static void sub_init_defaults(sub_part_t *sp)
{
    sp->magic = SUB_MAGIC;
    sp->sel_osc = 0;
    /* Osc 1: Saw, 100% mix, 0 pitch, 0 fold, 0 drive, 0 glide */
    sp->osc[0].wave = 85;
    sp->osc[0].fold = 0;
    sp->osc[0].mix = 127;
    sp->osc[0].pitch = 0;
    sp->osc[0].drive = 0;
    sp->osc[0].glide = 0;

    /* Osc 2: Saw, 70% mix, 0 pitch, 0 fold, 0 drive, 0 glide */
    sp->osc[1].wave = 85;
    sp->osc[1].fold = 0;
    sp->osc[1].mix = 89;
    sp->osc[1].pitch = 0;
    sp->osc[1].drive = 0;
    sp->osc[1].glide = 0;

    /* Osc 3: Tri, OFF mix (0), 0 pitch, 0 fold, 0 drive, 0 glide */
    sp->osc[2].wave = 42;
    sp->osc[2].fold = 0;
    sp->osc[2].mix = 0;
    sp->osc[2].pitch = 0;
    sp->osc[2].drive = 0;
    sp->osc[2].glide = 0;

    /* Osc 4 (SUB): Sine, OFF mix (0), -12 pitch (-1 octave), 0 fold, 0 drive, 0 glide */
    sp->osc[3].wave = 0;
    sp->osc[3].fold = 0;
    sp->osc[3].mix = 0;
    sp->osc[3].pitch = -12;
    sp->osc[3].drive = 0;
    sp->osc[3].glide = 0;

    for (uint32_t vi = 0; vi < NVOICE; vi++) {
        for (int o = 0; o < 4; o++) {
            sp->ph[vi][o] = (uint32_t)o * 0x40000000u;
            sp->cur_pitch16[vi][o] = 0;
        }
    }
}

static inline sub_part_t *sub_of(const track_t *t)
{
    uint32_t idx = (uint32_t)(t - trk) % NPART;
    sub_part_t *sp = &sub_state[idx];
    if (sp->magic != SUB_MAGIC) {
        sub_init_defaults(sp);
    }
    return sp;
}

/* 4-point smooth waveform morphing: Sine (0) -> Tri (42) -> Saw (85) -> Sqr (127) */
static inline int32_t osc_morphed(uint32_t ph, uint32_t inc, uint8_t wave)
{
    if (wave <= 42) {
        int32_t a = osc_sine(ph);
        int32_t b = osc_tri(ph);
        int32_t mix = (int32_t)wave * 780; /* 42 * 780 ~= 32760 */
        return a + (((b - a) * mix) >> 15);
    } else if (wave <= 85) {
        int32_t a = osc_tri(ph);
        int32_t b = osc_saw(ph, inc);
        int32_t mix = ((int32_t)wave - 42) * 762; /* 43 * 762 ~= 32766 */
        return a + (((b - a) * mix) >> 15);
    } else {
        int32_t a = osc_saw(ph, inc);
        int32_t b = osc_pulse(ph, inc, 0x80000000u);
        int32_t mix = ((int32_t)wave - 85) * 780; /* 42 * 780 ~= 32760 */
        return a + (((b - a) * mix) >> 15);
    }
}

/* Recursive wave folding (Loom Pi folding algorithm in Q15) */
static inline int32_t osc_fold(int32_t s, uint8_t fold)
{
    if (!fold) return s;
    int32_t th = 32767 - ((int32_t)fold * 29490 / 127);
    if (th < 3276) th = 3276;
    int iter = 0;
    while ((s > th || s < -th) && iter < 8) {
        if (s > th) s = th - (s - th);
        else if (s < -th) s = -th - (s + th);
        iter++;
    }
    int32_t scaled = (s << 15) / th;
    return clamp(scaled, -32768, 32767);
}

/* Analog drive with warm tanh soft clipping */
static inline int32_t osc_drive(int32_t s, uint8_t drv)
{
    if (!drv) return s;
    int32_t mult = 32768 + (int32_t)drv * 1290;
    int32_t ds = ((s >> 2) * (mult >> 2)) >> 11;
    return softclip(ds);
}

/* SVF tap (LP, BP, HP) */
static inline int32_t analog_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t ft)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    if (!ft)
        return v2;
    return ft == 1u ? v1 : in - ((k * v1) >> 12) - v2;
}
#define ANALOG_K(res) (8192 - (res) * 7600 / 127)     /* tsvf_coef's damping, Q12 */

static void analog_note_on(track_t *t, voice_t *v)
{
    sub_part_t *sp = sub_of(t);
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE;
    for (int o = 0; o < 4; o++) {
        sp->ph[vi][o] = (uint32_t)o * 0x40000000u;
        int32_t target_p16 = (int32_t)v->note * 16 + ((int32_t)sp->osc[o].pitch << 4);
        if (!sp->osc[o].glide || !sp->cur_pitch16[vi][o]) {
            sp->cur_pitch16[vi][o] = target_p16;
        }
    }
    v->s[0] = v->s[1] = 0;                            /* filter states */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    sub_part_t *sp = sub_of(t);
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE;
    const int16_t *p = t->p;

    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    uint32_t ft = (uint32_t)p[P_FTYPE];
    int32_t fk = ft ? ANALOG_K(p[P_E5]) : 0;
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[P_E5]);
    int32_t ic1 = v->s[0], ic2 = v->s[1];

    uint32_t inc[4];
    uint32_t ph[4];
    uint8_t active_osc[4];
    uint32_t n_active = 0;

    for (int o = 0; o < 4; o++) {
        ph[o] = sp->ph[vi][o];
        if (sp->osc[o].mix == 0) {
            active_osc[o] = 0;
            continue; /* Bypassed! Saves polyphony and DSP time */
        }
        active_osc[o] = 1;
        n_active++;

        /* Glide slewing */
        int32_t target_p16 = m->pitch16 + ((int32_t)sp->osc[o].pitch << 4);
        if (!sp->osc[o].glide) {
            sp->cur_pitch16[vi][o] = target_p16;
        } else {
            int32_t diff = target_p16 - sp->cur_pitch16[vi][o];
            int32_t max_step = 1 + (128 - sp->osc[o].glide) * 2;
            if (diff > max_step) sp->cur_pitch16[vi][o] += max_step;
            else if (diff < -max_step) sp->cur_pitch16[vi][o] -= max_step;
            else sp->cur_pitch16[vi][o] = target_p16;
        }
        uint32_t in = pitch_inc(clamp(sp->cur_pitch16[vi][o], 0, 2047));
        if (m->fine) {
            in += (uint32_t)((int32_t)(in >> 12) * m->fine);
        }
        inc[o] = in;
    }

    if (n_active == 0) {
        return;
    }

    for (uint32_t i = 0; i < n; i++) {
        int32_t sum = 0;
        for (int o = 0; o < 4; o++) {
            if (!active_osc[o]) continue;
            int32_t s = osc_morphed(ph[o], inc[o], sp->osc[o].wave);
            if (sp->osc[o].fold) {
                s = osc_fold(s, sp->osc[o].fold);
            }
            if (sp->osc[o].drive) {
                s = osc_drive(s, sp->osc[o].drive);
            }
            int32_t mix_q15 = (int32_t)sp->osc[o].mix * 258;
            sum += mulq15(s, mix_q15);
            ph[o] += inc[o];
        }

        int32_t y = ft ? analog_tap(&flt, fk, sum >> 1, &ic1, &ic2, ft) : tsvf_lp(&flt, sum >> 1, &ic1, &ic2);
        int32_t a = y < 0 ? -y : y;
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        int32_t sig = y << 1;
        out[i] += voice_amp(sig, m, i) << 1;
    }

    for (int o = 0; o < 4; o++) {
        sp->ph[vi][o] = ph[o];
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 85, 0, 127, 90, 30, 10, 64}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4)},
    {"SOFT PAD", {0, 85, 0, 100, 60, 10, 0, 32}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {0, 127, 0, 127, 50, 70, 40, 64}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {0, 85, 0, 110, 75, 20, 0, 48}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 85, 30, 127, 50, 100, 25, 64}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1)},
    {"SINE KEY", {0, 0, 0, 127, 127, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {0, 85, 10, 127, 85, 20, 30, 64}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), PAT(13)},
    {"SUB BASS", {3, 0, 0, 127, 40, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 42, 20, 127, 30, 40, 0, 64}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 85, 0, 127, 45, 20, 10, 64}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 100, 30, 90, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 85, 0, 110, 70, 10, 0, 32}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
};

static const engine_t ENG_ANALOG = {
    .name = "SUBTRACT",
    .page_title = {"OSC", "TUNE"},
    .edit = {
        {"OSC", F_INT, 0, 3, 0, 0, 0},
        {"WAVE", F_INT, 0, 127, 85, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"MIX", F_PCT, 0, 127, 127, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = ANALOG_PRESETS,
    .npresets = NELEM(ANALOG_PRESETS),
    .note_on = analog_note_on,
    .render = analog_render,
    .desc = 0,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .keep = 0x03,                /* the filter */
    .mem = sizeof(sub_part_t),
};
