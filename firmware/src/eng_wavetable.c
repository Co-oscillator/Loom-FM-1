/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAVETABLE: Loom Multi-Frame Wavetable Synthesis Engine.
 * Features 8 single-cycle frames (512 samples each), dynamic sample scanning
 * (Factory Piano, Flute, Sax, Break, User slots 1..3, or classic synth shapes),
 * phase warping, smooth inter-frame and intra-frame interpolation,
 * bit reduction, sample rate decimation, analog saturation drive, and SVF filtering. */

#define WT_NFRAMES 8u
#define WT_FRAME_LEN 512u
#define WT_FRAME_MASK (WT_FRAME_LEN - 1u)

typedef struct {
    uint8_t loaded_src;         /* current loaded SRC + 1 (0 = none) */
    uint8_t pad[3];
    int16_t table[WT_NFRAMES][WT_FRAME_LEN]; /* 8 * 512 * 2 = 8192 bytes */
} wt_part_t;

ENG_MEM_FITS("WAVETABLE", sizeof(wt_part_t));

static wt_part_t *wt_of(const track_t *t) { return (wt_part_t *)ENG_MEM(t - trk); }

/* Sources:
 * 0: BASIC  - 8 synthetic waveforms (Sine -> Tri -> Saw -> Square -> Pulse -> Reso -> Bell -> Vocal)
 * 1: PIANO  - Scanned from built-in middle C piano sample
 * 2: FLUTE  - Scanned from built-in acoustic flute sample
 * 3: SAX    - Scanned from built-in tenor sax sample
 * 4: BREAK  - Scanned from built-in breakbeat
 * 5: USR1   - Scanned from User Sample Slot 1
 * 6: USR2   - Scanned from User Sample Slot 2
 * 7: USR3   - Scanned from User Sample Slot 3 */
static const char *const N_WT_SRC[] = {"BASIC", "PIANO", "FLUTE", "SAX", "BREAK", "USR1", "USR2", "USR3"};
#define WT_NSRC 8u

/* Decode contiguous samples from an IMA ADPCM source into destination buffer,
 * with optional stride downsampling (stride = 4 for 2048-to-512 sample frames) */
static void wt_decode_adpcm_chunk(const uint8_t *data, uint32_t off, uint32_t total_len,
                                  uint32_t start_pos, int16_t *dst, uint32_t count, uint32_t stride)
{
    int32_t pred = 0;
    int32_t idx = 0;
    uint32_t p;

    if (!data || !total_len) {
        for (p = 0; p < count; p++) dst[p] = 0;
        return;
    }

    if (start_pos >= total_len) start_pos = total_len > 0u ? total_len - 1u : 0u;

    /* Pre-decode 256 samples before start_pos to accurately establish predictor state */
    uint32_t pre = start_pos > 256u ? start_pos - 256u : 0u;
    for (p = pre; p < start_pos; p++) {
        uint32_t byte_pos = off + (p >> 1);
        uint8_t b = data[byte_pos];
        uint32_t code = (p & 1u) ? (b >> 4) : (b & 15u);
        int32_t step = IMA_STEP[idx];
        int32_t vd = step >> 3;
        if (code & 4u) vd += step;
        if (code & 2u) vd += step >> 1;
        if (code & 1u) vd += step >> 2;
        pred = clamp(pred + ((code & 8u) ? -vd : vd), -32768, 32767);
        idx = clamp(idx + IMA_IDX[code & 7u], 0, 88);
    }

    /* Decode frame */
    uint32_t total_samples = count * stride;
    uint32_t accum = 0;
    int32_t sum = 0;
    uint32_t dst_idx = 0;

    for (p = 0; p < total_samples; p++) {
        uint32_t cur = start_pos + p;
        if (cur < total_len) {
            uint32_t byte_pos = off + (cur >> 1);
            uint8_t b = data[byte_pos];
            uint32_t code = (cur & 1u) ? (b >> 4) : (b & 15u);
            int32_t step = IMA_STEP[idx];
            int32_t vd = step >> 3;
            if (code & 4u) vd += step;
            if (code & 2u) vd += step >> 1;
            if (code & 1u) vd += step >> 2;
            pred = clamp(pred + ((code & 8u) ? -vd : vd), -32768, 32767);
            idx = clamp(idx + IMA_IDX[code & 7u], 0, 88);
            sum += pred;
        }
        accum++;
        if (accum == stride) {
            if (dst_idx < count) {
                dst[dst_idx++] = (int16_t)(sum / (int32_t)stride);
            }
            accum = 0;
            sum = 0;
        }
    }
    while (dst_idx < count) {
        dst[dst_idx++] = 0;
    }
}

/* Circular cross-correlation phase alignment: rotates target frame by the shift
 * that maximizes correlation with ref frame, preventing destructive phase cancellation */
static void wt_align_frame(int16_t *target, const int16_t *ref, uint32_t len)
{
    int64_t best_corr = -0x7FFFFFFFFFFFFFFFLL;
    uint32_t best_shift = 0;
    uint32_t s;
    for (s = 0; s < len; s += 2u) {
        int64_t corr = 0;
        uint32_t i;
        for (i = 0; i < len; i += 4u) {
            uint32_t ti = (i + s) & (len - 1u);
            corr += (int64_t)ref[i] * target[ti];
        }
        if (corr > best_corr) {
            best_corr = corr;
            best_shift = s;
        }
    }
    if (best_shift > 0) {
        int16_t temp[WT_FRAME_LEN];
        uint32_t i;
        for (i = 0; i < len; i++) temp[i] = target[(i + best_shift) & (len - 1u)];
        for (i = 0; i < len; i++) target[i] = temp[i];
    }
}

/* Post-process single-cycle frame: remove DC offset, peak normalize to 28000,
 * and smooth loop boundary with a gentle cosine taper */
static void wt_normalize_frame(int16_t *frame, uint32_t len)
{
    int32_t sum = 0;
    uint32_t i;
    for (i = 0; i < len; i++) sum += frame[i];
    int32_t dc = sum / (int32_t)len;

    /* Subtract DC and find peak amplitude */
    int32_t peak = 0;
    for (i = 0; i < len; i++) {
        int32_t s = frame[i] - dc;
        frame[i] = (int16_t)clamp(s, -32768, 32767);
        int32_t a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }

    /* Peak normalize frame to full scale (28000) so every frame across the wavetable
     * has strong, consistent volume without quiet spots or dropouts */
    if (peak > 32) {
        int32_t gain_q12 = (28000 << 12) / peak;
        for (i = 0; i < len; i++) {
            int32_t v = ((int32_t)frame[i] * gain_q12) >> 12;
            frame[i] = (int16_t)clamp(v, -32767, 32767);
        }
    }

    /* Smooth boundary with a gentle 16-sample cosine taper at endpoints
     * to eliminate any boundary phase clicks / discontinuities on loop wrap */
    const uint32_t taper_len = 16u;
    for (i = 0; i < taper_len; i++) {
        uint32_t ph = ((i * 2u + 1u) << 30) / taper_len; /* 0 .. pi in 32-bit phase */
        int32_t c_val = sine_i(ph - 0x40000000u);        /* -cos(x) in Q15 (-32768..32767) */
        int32_t w_q15 = (32768 + c_val) >> 1;            /* 0.5 * (1 - cos(x)) in Q15 */
        frame[i] = (int16_t)(((int32_t)frame[i] * w_q15) >> 15);
        frame[len - 1u - i] = (int16_t)(((int32_t)frame[len - 1u - i] * w_q15) >> 15);
    }
}

/* Build synthetic 8-frame wavetable for BASIC with aligned zero-crossings */
static void wt_build_basic(wt_part_t *wp)
{
    uint32_t f, i;
    for (f = 0; f < WT_NFRAMES; f++) {
        for (i = 0; i < WT_FRAME_LEN; i++) {
            uint32_t ph = i << (32 - 9); /* 0 .. 2^32-1 over 512 samples */
            int32_t val = 0;             /* Q15: -32768..32767 */

            switch (f) {
            case 0: /* Pure Sine */
                val = sine_i(ph);
                break;
            case 1: /* Triangle (starts at 0, peaks at 0.25, trough at 0.75, in phase with sine) */
                if (i < 128u) val = (int32_t)(i * 256u);
                else if (i < 384u) val = 65536 - (int32_t)(i * 256u);
                else val = (int32_t)(i * 256u) - 131072;
                break;
            case 2: /* Sawtooth (rises in first half, in phase with sine fundamental) */
                val = (i < 256u) ? (int32_t)(i * 128u) : ((int32_t)(i * 128u) - 65536);
                break;
            case 3: /* Square (50% pulse, in phase with sine) */
                val = (i < 256u) ? 30000 : -30000;
                break;
            case 4: /* Narrow Pulse (25% pulse width, in phase) */
                val = (i < 128u) ? 30000 : -10000;
                break;
            case 5: { /* Resonant sweep / sync wave */
                int32_t s1 = sine_i(ph);
                int32_t c3 = sine_i(ph * 3u + 0x40000000u);
                val = (s1 * (32768 + ((c3 * 19660) >> 15))) >> 15;
                break;
            }
            case 6: /* Bell / Metallic overtones */
                val = (sine_i(ph) * 21299 + sine_i(ph * 3u) * 11468) >> 15;
                break;
            case 7: /* Vocal Formant "Aah" */
                val = (sine_i(ph) * 18022 + sine_i(ph * 3u) * 11468 + sine_i(ph * 5u) * 6553) >> 15;
                break;
            default:
                val = sine_i(ph);
                break;
            }

            wp->table[f][i] = (int16_t)clamp(val, -32767, 32767);
        }
        wt_normalize_frame(wp->table[f], WT_FRAME_LEN);
    }
}

/* Load or scan wavetable frames for the given source into wt_part_t */
static void wt_load_source(wt_part_t *wp, uint32_t src)
{
    uint32_t f;

    if (src == 0) {
        wt_build_basic(wp);
        wp->loaded_src = 1;
        return;
    }

    /* Sample sources */
    const uint8_t *data = 0;
    uint32_t off = 0;
    uint32_t total_len = 0;

    if (src == 1) { /* PIANO (middle C zone 2: rich acoustic harmonics) */
        data = SMP_DATA;
        off = SMP_ZONES[2].off;
        total_len = SMP_ZONES[2].n;
    } else if (src == 2) { /* FLUTE (zone 5) */
        data = SMP_DATA;
        off = SMP_ZONES[5].off;
        total_len = SMP_ZONES[5].n;
    } else if (src == 3) { /* SAX (zone 8) */
        data = SMP_DATA;
        off = SMP_ZONES[8].off;
        total_len = SMP_ZONES[8].n;
    } else if (src == 4) { /* BREAK */
        const slc_src_t *sb = slc_get(0);
        if (sb && sb->len && sb->nseg) {
            data = SMP_DATA;
            off = sb->seg[0].off;
            total_len = sb->len;
        }
    } else if (src >= 5 && src <= 7) { /* USR1 .. USR3 */
        uint32_t slot = src - 5u;
        if (usr_nz[slot]) {
            data = SMP_DATA;
            off = usr_zone[slot][0].off;
            total_len = usr_zone[slot][0].n;
        }
    }

    if (!data || total_len < WT_FRAME_LEN) {
        /* Fallback if source sample is missing or empty */
        wt_build_basic(wp);
        wp->loaded_src = (uint8_t)(src + 1u);
        return;
    }

    /* Standard Serum/Loom wavetables are multiples of 2048 samples:
     * extract frames directly at 2048 stride with 4x downsampling box filtering */
    if (total_len >= 2048u && (total_len % 2048u == 0)) {
        uint32_t num_source_frames = total_len / 2048u;
        for (f = 0; f < WT_NFRAMES; f++) {
            uint32_t sf = num_source_frames > 1u ? (f * (num_source_frames - 1u)) / (WT_NFRAMES - 1u) : 0u;
            uint32_t start_pos = sf * 2048u;
            wt_decode_adpcm_chunk(data, off, total_len, start_pos, wp->table[f], WT_FRAME_LEN, 4u);
            wt_normalize_frame(wp->table[f], WT_FRAME_LEN);
        }
    } else {
        /* Acoustic audio samples: space frames across the entire sample,
         * and phase-align each frame to the previous frame to prevent cancellation */
        uint32_t scan_start = 0;
        uint32_t scan_end = total_len > WT_FRAME_LEN ? total_len - WT_FRAME_LEN : 0u;
        uint32_t scan_range = scan_end > scan_start ? scan_end - scan_start : 0u;

        for (f = 0; f < WT_NFRAMES; f++) {
            uint32_t start_pos = scan_start + (scan_range * f) / (WT_NFRAMES - 1u);
            wt_decode_adpcm_chunk(data, off, total_len, start_pos, wp->table[f], WT_FRAME_LEN, 1u);
            wt_normalize_frame(wp->table[f], WT_FRAME_LEN);
            if (f > 0) {
                wt_align_frame(wp->table[f], wp->table[f - 1u], WT_FRAME_LEN);
            }
        }
    }

    wp->loaded_src = (uint8_t)(src + 1u);
}

/* Continuous power-law phase warp in Q16 (0..65535), warp in -64..+63 */
static inline uint32_t wt_pow_q16(uint32_t x, uint32_t w)
{
    uint32_t x2 = (x * x) >> 16;
    uint32_t x4 = (x2 * x2) >> 16;
    if (w <= 26u) {
        uint32_t t = (w << 8) / 26u;
        return (uint32_t)((int32_t)x + ((((int32_t)x2 - (int32_t)x) * (int32_t)t) >> 8));
    } else {
        uint32_t t = ((w - 26u) << 8) / 38u;
        if (t > 256u) t = 256u;
        return (uint32_t)((int32_t)x2 + ((((int32_t)x4 - (int32_t)x2) * (int32_t)t) >> 8));
    }
}

static inline uint32_t wt_warp_phase_q16(uint32_t p, int32_t warp)
{
    if (warp > 1) {
        return wt_pow_q16(p, (uint32_t)warp);
    } else if (warp < -1) {
        return 65535u - wt_pow_q16(65535u - p, (uint32_t)(-warp));
    }
    return p;
}

static void wt_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0; /* Reset phase accumulator */
    v->s[0] = 0;  /* Last decimation sample */
    v->s[1] = 0;  /* Decimation counter */
    v->s[2] = 0;  /* SVF ic1eq */
    v->s[3] = 0;  /* SVF ic2eq */
}

/* Render audio block for one voice */
static void wt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    wt_part_t *wp = wt_of(t);
    uint32_t src = (uint32_t)clamp(t->p[P_E0], 0, WT_NSRC - 1u);

    if (wp->loaded_src != (uint8_t)(src + 1u)) {
        wt_load_source(wp, src);
    }

    /* Position (P_E1: 0..127) modulated by Shape, with (64 << 8) modulation bias subtracted */
    int32_t posq = clamp((t->p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    int32_t pos_scaled = (posq * 7) / 127; /* 0 .. (7 << 8) */
    int frame0 = pos_scaled >> 8;
    if (frame0 > 7) frame0 = 7;
    int frame1 = (frame0 < (int)(WT_NFRAMES - 1u)) ? frame0 + 1 : (int)(WT_NFRAMES - 1u);
    int32_t pos_frac = pos_scaled & 0xFF; /* 0..255 */

    /* Warp (P_E2: -64..+63, 0 = no warp) */
    int32_t warp = clamp(t->p[P_E2], -64, 63);

    /* Detune spread (P_E3: 0..127) */
    int32_t detune = t->p[P_E3];
    uint32_t inc = m->inc;
    if (detune && v->fine) {
        inc += (inc * ((detune * v->fine) >> 7)) >> 14;
    }

    /* Bits (P_E4: 0..127) -> 16 to 2 bits */
    int32_t bits = t->p[P_E4];
    int32_t bit_shift = (bits < 120) ? (((120 - bits) * 13) / 120) : 0;

    /* Decimation / Sample Rate reduction (P_E5: 0..127) */
    int32_t srat = t->p[P_E5];
    uint32_t srate_period = srat > 4 ? (1u + (uint32_t)srat / 4u) : 1u;

    /* Drive (P_E6: 0..127): 1.0x to 5.0x saturation gain */
    int32_t drive = t->p[P_E6];
    int32_t drv_gain = 4096 + drive * 128; // Q12: 1.0 .. 5.0

    /* Base filter cutoff is fully open (127 << 8). Modulated by filter env & LFO (m->cutoff) */
    int32_t cut = clamp((127 << 8) + m->cutoff, 0, 127 << 8);
    int32_t lp_k;
    if (cut >= (125 << 8)) {
        lp_k = 65536; /* Wide open bypass: crisp full harmonic spectrum */
    } else {
        uint32_t g = SVF_G[cut >> 8];
        lp_k = (int32_t)((g << 16) / (4096u + g));
    }

    uint32_t i;
    for (i = 0; i < n; i++) {
        v->ph[0] += inc;

        /* Decimation check */
        if (++v->s[1] >= (int32_t)srate_period) {
            v->s[1] = 0;

            /* Normalized phase in Q16 (0..65535) */
            uint32_t p_q16 = wt_warp_phase_q16(v->ph[0] >> 16, warp);

            /* Interpolation inside 512-sample wavetable frames:
             * p_q16 has 9 integer bits (0..511) and 7 fractional bits (0..127) */
            uint32_t i0 = (p_q16 >> 7) & WT_FRAME_MASK;
            uint32_t i1 = (i0 + 1u) & WT_FRAME_MASK;
            int32_t f_frac = (int32_t)(p_q16 & 127u);

            int32_t f0_0 = wp->table[frame0][i0];
            int32_t f0_1 = wp->table[frame0][i1];
            int32_t s0 = f0_0 + (((f0_1 - f0_0) * f_frac) >> 7);

            int32_t f1_0 = wp->table[frame1][i0];
            int32_t f1_1 = wp->table[frame1][i1];
            int32_t s1 = f1_0 + (((f1_1 - f1_0) * f_frac) >> 7);

            /* Crossfade between frames */
            int32_t s = s0 + (((s1 - s0) * pos_frac) >> 8);

            /* Bit reduction */
            if (bit_shift > 0) {
                s = (s >> bit_shift) << bit_shift;
            }

            v->s[0] = clamp(s, -32768, 32767);
        }

        int32_t sample = v->s[0];

        /* Drive / saturation */
        if (drive > 0) {
            int32_t driven = (sample * drv_gain) >> 12;
            sample = softclip(driven);
        }

        /* 1-pole Lowpass Filter */
        int32_t lp = sample;
        if (lp_k < 65536) {
            int32_t diff = sample - v->s[2];
            lp = v->s[2] + (int32_t)(((int64_t)diff * lp_k) >> 16);
            v->s[2] = lp;
        } else {
            v->s[2] = sample;
        }

        /* Scale by ADSR amplitude and output level convention matching system */
        out[i] += voice_amp(lp, m, i) << 1;
    }
}

/* Edit page descriptors:
 * Page 1: TABLE - SRC, POS, WARP, DETN
 * Page 2: TONE  - BITS, SRAT, DRV, GLID */
static const param_desc_t WT_EDIT[8] = {
    [0] = {"SRC", F_ENUM, 0, WT_NSRC - 1, 0, N_WT_SRC, 0},
    [1] = {"POS", F_PCT, 0, 127, 32, 0, 0},
    [2] = {"WARP", F_BIPCT, -64, 63, 0, 0, 0},
    [3] = {"DETN", F_PCT, 0, 127, 20, 0, 0},
    [4] = {"BITS", F_PCT, 0, 127, 127, 0, 0},
    [5] = {"SRAT", F_PCT, 0, 127, 0, 0, 0},
    [6] = {"DRV", F_PCT, 0, 127, 16, 0, 0},
    [7] = {"GLID", F_TIME, 0, 127, 0, 0, 0},
};

static const preset_t WT_PRESETS[] = {
    /* name, {SRC, POS, WARP, DETN, BITS, SRAT, DRV, GLID}, {A, D, S, R}, fenv, mono, FX(d, c, dl, r) */
    {"ANLG WT", {0, 20, 0, 15, 127, 0, 20, 0}, {5, 60, 110, 45}, 0, 0, FX(0, 0, 0, 0)},
    {"PIANO RES", {1, 45, 25, 10, 127, 0, 10, 0}, {0, 80, 80, 55}, 0, 0, FX(0, 0, 0, 0)},
    {"AERO FLUT", {2, 60, -15, 20, 127, 0, 0, 15}, {25, 90, 120, 65}, 0, 0, FX(0, 10, 0, 20)},
    {"BRASS SAX", {3, 35, 30, 25, 127, 0, 45, 10}, {10, 75, 100, 40}, 0, 0, FX(15, 0, 0, 0)},
    {"NEON TAPE", {4, 70, 0, 35, 95, 15, 50, 0}, {0, 70, 90, 50}, 0, 0, FX(25, 0, 10, 15)},
    {"DIGI BITS", {0, 85, 40, 0, 45, 55, 30, 0}, {0, 50, 70, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"CYBERVOX",  {0, 115, -20, 30, 127, 0, 35, 20}, {20, 85, 115, 75}, 0, 0, FX(0, 12, 0, 20)},
    {"SUB BASS",  {0, 10, 10, 5, 127, 0, 65, 0}, {0, 60, 100, 35}, 0, 1, FX(30, 0, 0, 0)},
};

static const engine_t ENG_WAVETABLE = {
    .name = "WAVETABLE",
    .page_title = {"TABLE", "TONE"},
    .edit = {
        [0] = {"SRC", F_ENUM, 0, WT_NSRC - 1, 0, N_WT_SRC, 0},
        [1] = {"POS", F_PCT, 0, 127, 32, 0, 0},
        [2] = {"WARP", F_BIPCT, -64, 63, 0, 0, 0},
        [3] = {"DETN", F_PCT, 0, 127, 20, 0, 0},
        [4] = {"BITS", F_PCT, 0, 127, 127, 0, 0},
        [5] = {"SRAT", F_PCT, 0, 127, 0, 0, 0},
        [6] = {"DRV", F_PCT, 0, 127, 16, 0, 0},
        [7] = {"GLID", F_TIME, 0, 127, 0, 0, 0},
    },
    .presets = WT_PRESETS,
    .npresets = (uint8_t)(sizeof(WT_PRESETS) / sizeof(WT_PRESETS[0])),
    .knob = {P_E1, P_E2, P_ED_FLT, P_E6}, /* POS, WARP, FILTER, DRIVE */
    .poly = 0,                            /* full 8-voice polyphony */
    .sampled = 0,                         /* free-running phase oscillator */
    .keep = 0,
    .oneshot = 0,
    .note_on = wt_note_on,
    .render = wt_render,
    .amp = 0,
    .desc = 0,
    .block = 0,
    .keys = 0,
    .ownenv = 0,
    .done = 0,
    .mem = sizeof(wt_part_t),
};
