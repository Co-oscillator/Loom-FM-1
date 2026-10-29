/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SCRUB: Tape & Turntable Platter Scrubbing Playback Engine.
 * Features 4-point cubic Hermite interpolation, rubbery spring tracking physics,
 * sliding PCM cache window, speed-adaptive magnetic tape head filtering,
 * continuous tape motor speed, loop toggle, and tape saturation. */

static inline int32_t scrb_abs32(int32_t x) { return x < 0 ? -x : x; }
static inline int64_t scrb_abs64(int64_t x) { return x < 0 ? -x : x; }

#define SCRUB_CACHE_SIZE 512u

typedef struct {
    int64_t pos;                /* current playback position in Q16 sample frames */
    int64_t target_pos;         /* target position in Q16 sample frames */
    int64_t smoothed_target;    /* smoothed target position in Q16 sample frames */
    int64_t move_span;          /* how far the last POS move asked the playhead to go (Q16) */
    int32_t velocity;           /* current speed in Q16 samples / sample (65536 = 1.0x) */
    int32_t prev_step;          /* previous block sample step in Q16 for sub-block smoothing */
    int16_t last_scrub_param;   /* previous POS knob value */
    uint16_t touch_timer;       /* interaction hangover timer */
    uint8_t last_src;           /* previous sample source */
    uint8_t auto_gate;          /* the voice was started by the auto-play path, not a note */
    int32_t lp_state;           /* tape head low-pass state */

    /* PCM Cache Window for 4-point cubic Hermite interpolation */
    uint32_t cache_start;       /* sample index of cache[0] */
    uint32_t cache_count;       /* number of valid samples in cache */
    int16_t cache[SCRUB_CACHE_SIZE]; /* 512 cached 16-bit PCM samples */
} scrb_part_t;

ENG_MEM_FITS("SCRUB", sizeof(scrb_part_t));

static scrb_part_t *scrb_of(const track_t *t) { return (scrb_part_t *)ENG_MEM(t - trk); }

/* 4-point, 3rd-order Hermite Interpolation (Cubic) in Q15 fixed-point.
 * Guarantees C1 derivative continuity across sample boundaries,
 * eliminating the clicks/blips of linear interpolation at slow scrub speeds. */
static inline int32_t cubic_hermite_q15(int32_t y0, int32_t y1, int32_t y2, int32_t y3, int32_t mu_q15)
{
    int32_t a0 = ((3 * (y1 - y2) - y0 + y3) >> 1);
    int32_t a1 = y0 + 2 * y2 - ((5 * y1 + y3) >> 1);
    int32_t a2 = (y2 - y0) >> 1;
    int32_t t = (int32_t)(((int64_t)a0 * mu_q15) >> 15) + a1;
    t = (int32_t)(((int64_t)t * mu_q15) >> 15) + a2;
    return (int32_t)(((int64_t)t * mu_q15) >> 15) + y1;
}

static const char *const N_SCRUB_LOOP[] = {"OFF", "FWD", "REV", "PING"};

static inline void scrb_wrap_pos(scrb_part_t *sp, uint32_t len, int loop_mode, int32_t *cur_step)
{
    int64_t max_pos = (len > 1u) ? ((int64_t)(len - 1u) << 16) : 0;
    if (max_pos <= 0) {
        sp->pos = 0;
        return;
    }
    if (loop_mode == 3) { /* PING-PONG bounce */
        if (sp->pos >= max_pos) {
            sp->pos = 2 * max_pos - sp->pos;
            if (sp->pos < 0) sp->pos = 0;
            if (sp->velocity > 0) sp->velocity = -sp->velocity;
            if (cur_step && *cur_step > 0) *cur_step = -*cur_step;
        } else if (sp->pos < 0) {
            sp->pos = -sp->pos;
            if (sp->pos > max_pos) sp->pos = max_pos;
            if (sp->velocity < 0) sp->velocity = -sp->velocity;
            if (cur_step && *cur_step < 0) *cur_step = -*cur_step;
        }
    } else if (loop_mode != 0) { /* WRAP (FWD / REV) */
        while (sp->pos >= max_pos) sp->pos -= max_pos;
        while (sp->pos < 0) sp->pos += max_pos;
    } else { /* OFF */
        if (sp->pos < 0) {
            sp->pos = 0;
            sp->velocity = 0;
            if (cur_step) *cur_step = 0;
        } else if (sp->pos >= max_pos) {
            sp->pos = max_pos;
            sp->velocity = 0;
            if (cur_step) *cur_step = 0;
        }
    }
}

/* Fill the sliding PCM cache window from the ADPCM source */
static void scrb_fill_cache(const slc_src_t *s, scrb_part_t *sp, uint32_t start_pos)
{
    if (!s || !s->len) {
        sp->cache_count = 0;
        return;
    }
    if (start_pos >= s->len) start_pos = s->len - 1u;

    uint32_t k = (start_pos << SLC_GRID_LOG2) / s->len;
    k = k < SLC_GRID ? k : SLC_GRID - 1u;
    while (k && slc_gpos(s, k) > start_pos) k--;
    while (k + 1u < SLC_GRID && slc_gpos(s, k + 1u) <= start_pos) k++;

    slc_dec_t d;
    slc_dec_at(&d, slc_gpos(s, k), s->grid[k]);
    while (d.pos < start_pos) {
        slc_dec_next(s, &d);
    }

    uint32_t count = 0;
    while (count < SCRUB_CACHE_SIZE && d.pos < s->len) {
        sp->cache[count++] = (int16_t)slc_dec_next(s, &d);
    }
    sp->cache_start = start_pos;
    sp->cache_count = count;
}

/* Retrieve an uncompressed PCM sample, refilling the cache window on miss */
static int16_t scrb_sample_val(const slc_src_t *s, scrb_part_t *sp, int32_t idx, int loop_mode)
{
    if (!s || !s->len) return 0;
    if (loop_mode == 3) { /* PING-PONG reflection */
        if (idx < 0) {
            idx = -idx;
        } else if ((uint32_t)idx >= s->len) {
            idx = (int32_t)(2 * (s->len - 1u) - (uint32_t)idx);
        }
        if (idx < 0) idx = 0;
        else if ((uint32_t)idx >= s->len) idx = (int32_t)s->len - 1;
    } else if (loop_mode != 0) {
        if (idx < 0) {
            idx = (int32_t)s->len - 1 + ((idx + 1) % (int32_t)s->len);
        } else if ((uint32_t)idx >= s->len) {
            idx = idx % (int32_t)s->len;
        }
    } else {
        if (idx < 0) idx = 0;
        else if ((uint32_t)idx >= s->len) idx = (int32_t)s->len - 1;
    }

    if (sp->cache_count > 0 &&
        (uint32_t)idx >= sp->cache_start &&
        (uint32_t)idx < sp->cache_start + sp->cache_count) {
        return sp->cache[(uint32_t)idx - sp->cache_start];
    }

    /* Cache miss: center new window around idx with 128-sample backward headroom */
    uint32_t start = ((uint32_t)idx >= (SCRUB_CACHE_SIZE / 4u)) ? (uint32_t)idx - (SCRUB_CACHE_SIZE / 4u) : 0u;
    scrb_fill_cache(s, sp, start);

    if (sp->cache_count > 0 &&
        (uint32_t)idx >= sp->cache_start &&
        (uint32_t)idx < sp->cache_start + sp->cache_count) {
        return sp->cache[(uint32_t)idx - sp->cache_start];
    }
    return 0;
}

static void scrub_note_on(track_t *t, voice_t *v)
{
    scrb_part_t *sp = scrb_of(t);
    const slc_src_t *s = slc_get(clamp(t->p[P_E4], 0, SLC_NSRC - 1));
    if (!s || !s->len) {
        v->s[6] = 2; /* sine fallback if empty */
        return;
    }
    /* Key cues to note position across the tape */
    uint32_t note_idx = (v->note >= 48) ? (v->note - 48) % 24 : v->note % 24;
    sp->pos = ((int64_t)((note_idx * (s->len - 1u)) / 24u)) << 16;
    sp->target_pos = sp->pos;
    sp->smoothed_target = sp->pos;
    sp->touch_timer = 0;
    sp->velocity = 0;
    sp->prev_step = 0;
    sp->auto_gate = 0;   /* this voice belongs to the note, not to the auto-play path */
    sp->cache_count = 0; /* force cache refill */
    v->s[6] = 0;
}

static void scrub_block(track_t *t)
{
    scrb_part_t *sp = scrb_of(t);
    const int16_t *p = t->p;
    const slc_src_t *s = slc_get(clamp(p[P_E4], 0, SLC_NSRC - 1));
    if (!sp || !s || !s->len) return;

    /* Check if sample source changed */
    if (p[P_E4] != sp->last_src) {
        sp->last_src = (uint8_t)p[P_E4];
        sp->cache_count = 0;
    }

    /* 1. Update Physics (runs at 44100 / 32 = 1378.125 Hz, dt = 0.7256 ms) */
    if (p[P_E0] != sp->last_scrub_param) {
        if (sp->touch_timer == 0) {
            /* First touch from idle (the motor may be running): start from the current playhead */
            sp->smoothed_target = sp->pos;
        }
        sp->last_scrub_param = p[P_E0];
        uint32_t pct = (uint32_t)clamp(p[P_E0], 0, 100);
        uint32_t q16_pct = pct * 655u + (pct * 36u) / 100u;
        sp->target_pos = (int64_t)(s->len - 1u) * (int64_t)q16_pct;
        sp->move_span = sp->target_pos - sp->pos;
        if (sp->move_span < 0) sp->move_span = -sp->move_span;
        sp->touch_timer = 1100; /* ~800ms hangover window so slow encoder turns stay in continuous scrub mode */
    }
    if (sp->touch_timer > 0) sp->touch_timer--;

    /* Motor target speed: -3.0x to +3.0x in Q16 (p[P_E1] in -30..30) */
    int32_t motor_speed = ((int32_t)p[P_E1] * 65536) / 10;
    if (p[P_E1] == 0 && (song.playing || (t->v[0].active && t->v[0].gate))) {
        motor_speed = (p[P_E5] == 2) ? -65536 : 65536; /* REV runs in reverse at -1x */
    }

    if (sp->touch_timer > 0) {
        /* User is actively scrubbing: continuous mass-spring-damper reel tracking.
         * Tuned for 1378 Hz block rate so discrete 1% encoder steps blend into a
         * single continuous glide with smooth pitch shifting rather than discrete pops. */
        int32_t inert = clamp(p[P_E2], 0, 100);
        /* Stage 1: smooth discrete encoder steps over ~15ms..60ms */
        int32_t slew_alpha = 1600 - (inert * 1200) / 100; /* Q15 per 0.725ms block */
        int64_t pos_diff = sp->target_pos - sp->smoothed_target;
        int64_t pos_step = (pos_diff * slew_alpha) / 32768;
        if (pos_step == 0 && pos_diff != 0) pos_step = pos_diff > 0 ? 1 : -1;
        sp->smoothed_target += pos_step;

        /* Stage 2: proportional velocity servo (error in Q16 source samples -> velocity in Q16).
         * Bridges encoder clicks into continuous tape movement with smooth pitch shifting. */
        int64_t error = sp->smoothed_target - sp->pos;
        int32_t k_vel = 24 + ((100 - inert) * 44) / 100;   /* more torque: the first detent moves at once */
        int64_t target_vel64 = (error * k_vel) / 32768;
        int32_t target_vel = (int32_t)(target_vel64 > 262144 ? 262144 : (target_vel64 < -262144 ? -262144 : target_vel64));

        /* Platter momentum filter: smooth acceleration/deceleration */
        int32_t track_rate = 600 + ((100 - inert) * 1200) / 100; /* Q15 per 0.725ms block */
        int32_t vel_diff = target_vel - sp->velocity;
        int32_t vel_step = (int32_t)(((int64_t)vel_diff * track_rate) / 32768);
        if (vel_step == 0 && vel_diff != 0) vel_step = vel_diff > 0 ? 1 : -1;
        sp->velocity += vel_step;

        if (scrb_abs32(motor_speed) > 655) {            /* the motor wants to run: hand back as soon as the move is done */
            int64_t tol = sp->move_span / 8;            /* (a fraction of the move: it never skips a small nudge) */
            if (tol < ((int64_t)64 << 16)) tol = (int64_t)64 << 16;
            if (tol > ((int64_t)512 << 16)) tol = (int64_t)512 << 16;
            if (scrb_abs64(sp->target_pos - sp->pos) < tol && scrb_abs64(sp->target_pos - sp->smoothed_target) < tol)
                sp->touch_timer = 0;
        }
        if (scrb_abs64(error) < 16384 && scrb_abs64(sp->target_pos - sp->pos) < 65536 && scrb_abs32(sp->velocity) < 256) {
            sp->pos = sp->target_pos;
            sp->smoothed_target = sp->target_pos;
            sp->velocity = 0;
            if (scrb_abs32(motor_speed) > 655)          /* landed: the motor takes over now, not after the hangover */
                sp->touch_timer = 0;
        }
    } else if (scrb_abs32(motor_speed) > 655) {
        /* Motor is running: accelerate/decelerate toward motor speed with inertia */
        int32_t inertia = 48 + ((100 - clamp(p[P_E2], 0, 100)) * 420) / 100;
        sp->velocity += (int32_t)(((int64_t)(motor_speed - sp->velocity) * inertia) >> 15);
    } else {
        /* Motor is stopped: platter/tape friction decay */
        int32_t friction = 32680 - (clamp(p[P_E3], 0, 100) * 600) / 100;
        sp->velocity = (int32_t)(((int64_t)sp->velocity * friction) >> 15);
        int32_t coulomb = 8;
        if (sp->velocity > 0) sp->velocity = sp->velocity > coulomb ? sp->velocity - coulomb : 0;
        else if (sp->velocity < 0) sp->velocity = sp->velocity < -coulomb ? sp->velocity + coulomb : 0;
        /* Firm deadband at 0 to prevent stationary microsample rocking */
        if (scrb_abs32(sp->velocity) < 32) sp->velocity = 0;
    }

    /* Clamp maximum speed (-4.0x .. +4.0x in Q16) */
    if (sp->velocity > 262144) sp->velocity = 262144;
    if (sp->velocity < -262144) sp->velocity = -262144;

    /* Advance position for this block (CTL = 32 samples) if voice is not actively rendering */
    if (!t->v[0].active) {
        int32_t step_q16 = (int32_t)(((int64_t)sp->velocity * (int64_t)s->rate) >> 16);
        sp->pos += (int64_t)step_q16 * (int64_t)CTL;
        scrb_wrap_pos(sp, s->len, p[P_E5], 0);
        sp->prev_step = step_q16;
    }

    /* Auto-activate voice 0 for a scrub (touch) or a running transport, so sound plays. A key's own note-on
     * already has its voice; velocity alone must not keep a voice alive (the motor may run silently). */
    if (!t->v[0].active && (sp->touch_timer > 0 || (song.playing && p[P_E1] != 0))) {
        voice_t *v = &t->v[0];
        v->active = 1;
        v->gate = 1;
        v->stage = 2; /* sustain */
        v->note = 60;
        v->vel = 127;
        v->env = 1 << 24;
        v->env_out = 32767;
        sp->auto_gate = 1;
    }
    /* The auto-play voice ends with the scrub (a note's release sets its own gate). The motor may keep
     * moving the tape silently. */
    if (t->v[0].active && t->v[0].gate && sp->auto_gate && !song.playing && sp->touch_timer == 0) {
        t->v[0].gate = 0;
        t->v[0].stage = 3;                              /* release, not simply let go of the gate */
        t->v[0].fstage = 3;
        sp->auto_gate = 0;
    }
}

static void scrub_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    scrb_part_t *sp = scrb_of(t);
    const int16_t *p = t->p;
    const slc_src_t *s = slc_get(clamp(p[P_E4], 0, SLC_NSRC - 1));
    if (!s || !s->len) {
        int32_t lp = 28000;
        smp_sine(out, n, m, 0, lp, &v->ph[0], &v->s[7]);
        return;
    }

    /* Advance rate scaled by sample rate (s->rate is source_rate / 44100 in Q16) */
    int32_t target_sample_step = (int32_t)(((int64_t)sp->velocity * (int64_t)s->rate) >> 16);

    /* Wow / flutter modulation */
    if (p[P_E7] > 0 && scrb_abs32(sp->velocity) > 256) {
        int32_t wow = (sine_i(v->ph[1] * 136957u) * (int32_t)p[P_E7] * 26) >> 15;
        target_sample_step += wow;
        v->ph[1] += n;
    }

    /* Sub-block sample step ramp across n samples to eliminate any block-rate step noise */
    int32_t cur_sample_step = sp->prev_step;
    int32_t step_inc = (target_sample_step - cur_sample_step) / (int32_t)n;

    /* Base filter cutoff modulated by envelope / LFO / macro */
    int32_t user_cutoff = 6000 + ((clamp(28000 + m->cutoff, 0, 127 << 8) * 26767) >> 15);

    /* Dynamic tape head frequency response:
     * Keep slow scrubbing warm and clear while gently rolling off ultrasonic stepping. */
    int32_t spd = scrb_abs32(sp->velocity);
    int32_t speed_factor_q15 = spd >> 1;
    if (speed_factor_q15 > 32768) speed_factor_q15 = 32768;
    int32_t lp_scale_q15 = 11000 + ((21768 * speed_factor_q15) >> 15);
    int32_t lp = clamp((user_cutoff * lp_scale_q15) >> 15, 4500, 32000);

    int32_t drv = p[P_E6];
    int loop_mode = p[P_E5];

    /* Keep audio full-level down to very slow scrub speeds (~0.02x), only fading to 0 at standstill */
    int32_t speed_scale_q15 = 0;
    if (spd >= 1024) {
        speed_scale_q15 = 32768;
    } else if (spd >= 32) {
        speed_scale_q15 = (spd * 32768) / 1024;
    }

    for (uint32_t i = 0; i < n; i++) {
        cur_sample_step += step_inc;
        sp->pos += cur_sample_step;
        scrb_wrap_pos(sp, s->len, loop_mode, &cur_sample_step);

        if (speed_scale_q15 <= 16) {
            sp->lp_state = (sp->lp_state * 31000) >> 15;
            continue;
        }

        /* 4-point cubic Hermite interpolation */
        int32_t p1 = (int32_t)(sp->pos >> 16);
        int32_t frac_q15 = (int32_t)((sp->pos >> 1) & 0x7FFF);

        int32_t y0 = scrb_sample_val(s, sp, p1 - 1, loop_mode);
        int32_t y1 = scrb_sample_val(s, sp, p1, loop_mode);
        int32_t y2 = scrb_sample_val(s, sp, p1 + 1, loop_mode);
        int32_t y3 = scrb_sample_val(s, sp, p1 + 2, loop_mode);

        int32_t s_val = clamp(cubic_hermite_q15(y0, y1, y2, y3, frac_q15), -32768, 32767);

        if (speed_scale_q15 < 32768) {
            s_val = (s_val * speed_scale_q15) >> 15;
        }

        if (drv) {
            s_val = softclip(s_val + (((s_val >> 2) * (drv * 150)) >> 11));
        }

        sp->lp_state += mulq15(s_val - sp->lp_state, lp);
        out[i] += voice_amp(sp->lp_state, m, i) << 1;
    }

    sp->prev_step = target_sample_step;
}

static int32_t scrb_amp(track_t *t, voice_t *v, int32_t adsr)
{
    scrb_part_t *sp = scrb_of(t);
    const int16_t *p = t->p;
    /* In Scrub mode, if motor is running or tape is still moving/scrubbing, sustain sound. A note that was
     * let go (gate 0) keeps its release: only a still-gated voice is held in sustain. */
    if (v->gate && (p[P_E1] != 0 || song.playing || sp->touch_timer > 0 || scrb_abs32(sp->velocity) > 64) &&
        v->stage == 3u) {
        v->stage = 2; /* keep sustaining */
    }
    return adsr;
}

static const preset_t SCRUB_PRESETS[] = {
    {"VINYL", {0, 0, 50, 40, 0, 0, 35, 15}, {0, 127, 127, 60}, 0, 1, FX(0, 0, 0, 15)},
    {"REEL",  {0, 10, 30, 20, 0, 1, 20, 5}, {0, 127, 127, 60}, 0, 1, FX(0, 0, 0, 0)},
    {"BRAKE", {0, 0, 80, 80, 0, 0, 50, 25}, {0, 127, 127, 60}, 0, 1, FX(0, 0, 0, 0)},
};

static const engine_t ENG_SCRUB = {
    .name = "SCRUB",
    .page_title = {"SCRB", "TAPE"},
    .edit = {
        {"POS", F_PCT, 0, 100, 0, 0, 0},
        {"MOTOR", F_XSPD, -30, 30, 10, 0, "x"},
        {"INERT", F_PCT, 0, 100, 50, 0, 0},
        {"FRIC", F_PCT, 0, 100, 50, 0, 0},
        {"SRC", F_ENUM, 0, SLC_NSRC - 1, 0, N_SLC_SRC, 0},
        {"LOOP", F_ENUM, 0, 3, 0, N_SCRUB_LOOP, 0},
        {"DRV", F_PCT, 0, 127, 30, 0, 0},
        {"WOW", F_PCT, 0, 100, 10, 0, 0},
    },
    .presets = SCRUB_PRESETS,
    .npresets = NELEM(SCRUB_PRESETS),
    .note_on = scrub_note_on,
    .render = scrub_render,
    .amp = scrb_amp,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = 1,
    .block = scrub_block,
    .sampled = 1,
    .mem = sizeof(scrb_part_t),
};
