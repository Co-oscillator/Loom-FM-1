/* SPDX-License-Identifier: GPL-3.0-only */
/* Loom FM-1: Procedural Analog & FM Drum Engines ported from Loom Pi */
#include "loom_drum.h"
#include <stdint.h>

#define FS_HZ 44100u

static inline int32_t loom_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* 32-bit LCG random generator */
static uint32_t g_noise_seed = 22222u;
static inline int32_t loom_rng(void)
{
    g_noise_seed = g_noise_seed * 196314165u + 907633515u;
    return (int32_t)(g_noise_seed >> 16) - 32768; /* Q15: -32768..32767 */
}

/* Lookup sine with linear interpolation, Q15 output (-32768..32767) */
extern int32_t sine_i(uint32_t ph);

/* ------------------------------------------------ Analog Drum Voice ------------------------------------------------ */
typedef struct {
    uint8_t active;
    uint8_t type;
    int32_t env;          /* Q24 envelope */
    int32_t env2;         /* Q24 secondary envelope (snare wires / clap) */
    int32_t pitch_env;    /* Q24 pitch envelope */
    uint32_t decay_coeff; /* Q16 (e.g. 0.999 * 65536) */
    uint32_t decay_coeff2;
    uint32_t pitch_decay_coeff;
    uint32_t phase;
    uint32_t base_inc;
    int32_t filter_state;
    int32_t filter_state2;
    /* Clap state */
    int32_t clap_timer;
    uint8_t clap_stage;
    int32_t clap_env;     /* Q24 */
    /* Hat / Cymbal 6-square oscillators */
    uint32_t hat_phase[6];
    uint32_t hat_inc[6];
    /* Attack phase for cymbal */
    int32_t attack_phase;
    int32_t attack_step;
    /* Macro settings */
    uint8_t vel;
    int16_t tone;
    int16_t snap;
    int16_t punch;
} analog_voice_t;

/* -------------------------------------------------- FM Drum Voice -------------------------------------------------- */
typedef struct {
    uint8_t active;
    uint8_t type;
    uint32_t phase_c;     /* carrier phase */
    uint32_t phase_m;     /* modulator phase */
    uint32_t inc_c;       /* carrier increment */
    uint32_t inc_m;       /* modulator increment */
    int32_t env_c;        /* Q24 carrier envelope */
    int32_t env_m;        /* Q24 modulator envelope */
    int32_t pitch_env;    /* Q24 pitch sweep */
    uint32_t decay_c;     /* Q16 decay */
    uint32_t decay_m;
    uint32_t decay_pitch;
    int32_t mod_level;    /* Q15 modulation index */
    int32_t feedback;     /* Q15 feedback amount */
    int32_t last_mod;     /* Q15 last modulator sample */
    uint8_t vel;
} fm_voice_t;

static analog_voice_t a_voice[LOOM_DRUM_LANES];
static fm_voice_t     f_voice[LOOM_DRUM_LANES];

/* Convert frequency in Q8 Hz (1/256 Hz) to 32-bit phase increment at 44.1 kHz */
static inline uint32_t hz_to_inc_q8(uint32_t hz_q8)
{
    return (uint32_t)(((uint64_t)hz_q8 * 97392ULL) >> 8);
}

/* Fast exponential decay coefficient in Q16 for time in milliseconds */
static inline uint32_t decay_ms_to_q16(uint32_t t_ms)
{
    if (t_ms < 1u) t_ms = 1u;
    uint32_t ns = (t_ms * 441u) / 10u;
    if (ns < 44u) ns = 44u;
    uint32_t sub = (65536u + (ns >> 1)) / ns;
    if (sub < 1u) sub = 1u;
    if (sub >= 65536u) return 0u;
    return 65536u - sub;
}

void loom_drum_init(void)
{
    memset(a_voice, 0, sizeof(a_voice));
    memset(f_voice, 0, sizeof(f_voice));
    for (uint32_t i = 0; i < LOOM_DRUM_LANES; i++) {
        a_voice[i].type = (uint8_t)i;
        f_voice[i].type = (uint8_t)i;
    }
}

void loom_drum_all_off(void)
{
    for (uint32_t i = 0; i < LOOM_DRUM_LANES; i++) {
        a_voice[i].active = 0;
        f_voice[i].active = 0;
    }
}

int loom_drum_active(void)
{
    for (uint32_t i = 0; i < LOOM_DRUM_LANES; i++) {
        if (a_voice[i].active || f_voice[i].active)
            return 1;
    }
    return 0;
}

void loom_drum_trigger(uint32_t lane, uint32_t vel, const int16_t *p)
{
    if (lane >= LOOM_DRUM_LANES || !vel)
        return;

    fx_sidechain_trigger(lane, vel);

    /* Closed hat chokes open hat */
    if (lane == LOOM_DRUM_HAT_CL) {
        if (a_voice[LOOM_DRUM_HAT_OP].active) a_voice[LOOM_DRUM_HAT_OP].env >>= 3;
        if (f_voice[LOOM_DRUM_HAT_OP].active) f_voice[LOOM_DRUM_HAT_OP].env_c >>= 3;
    }

    /* Read parameters (P_E0..P_E7) */
    int32_t tune  = p ? p[1] : 64; /* TUNE (0..127) */
    int32_t tone  = p ? p[2] : 64; /* TONE (0..127) */
    int32_t decy  = p ? p[3] : 64; /* DECY (0..127) */
    int32_t snap  = p ? p[4] : 64; /* SNAP (0..127) */
    int32_t punch = p ? p[6] : 64; /* PUNCH (0..127) */

    uint32_t d  = (uint32_t)loom_clamp(decy, 0, 127);
    uint32_t t  = (uint32_t)loom_clamp(tone, 0, 127);
    uint32_t sn = (uint32_t)loom_clamp(snap, 0, 127);
    uint32_t pu = (uint32_t)loom_clamp(punch, 0, 127);
    int32_t  tu = loom_clamp(tune, 0, 127) - 64; /* -64 .. +63 */
    uint32_t d2 = (d * d) / 127u;                /* 0 .. 127 */

    /* ---------------- Trigger Analog Voice ---------------- */
    analog_voice_t *av = &a_voice[lane];
    av->active = 1;
    av->vel = (uint8_t)vel;
    av->env = (1 << 24);
    av->env2 = (1 << 24);
    av->pitch_env = (1 << 24);
    av->phase = 0;
    av->filter_state = 0;
    av->filter_state2 = 0;
    av->clap_timer = 0;
    av->clap_stage = 0;
    av->clap_env = (1 << 24);
    av->attack_phase = 0;
    av->tone = (int16_t)tone;
    av->snap = (int16_t)snap;
    av->punch = (int16_t)punch;

    static const uint16_t HAT_RATIOS_Q8[6] = {256, 379, 404, 468, 548, 673};
    static const uint16_t CYM_RATIOS_Q8[6] = {256, 384, 417, 476, 553, 681};

    switch (lane) {
    case LOOM_DRUM_KICK: {
        uint32_t f0_q8 = (uint32_t)((48 * (160 + tu)) << 8) / 160u;
        av->base_inc = hz_to_inc_q8(f0_q8);
        uint32_t kick_ms = 150u + (d * 700u) / 127u + (d2 * 2650u) / 127u;
        av->decay_coeff = decay_ms_to_q16(kick_ms);
        uint32_t p_ms = 20u + (sn * 40u) / 127u;
        av->pitch_decay_coeff = decay_ms_to_q16(p_ms);
        break;
    }
    case LOOM_DRUM_SNARE: {
        uint32_t f0_q8 = (uint32_t)((160 * (160 + tu)) << 8) / 160u;
        av->base_inc = hz_to_inc_q8(f0_q8);
        uint32_t shell_ms = 50u + (d * 150u) / 127u + (d2 * 500u) / 127u;
        uint32_t wires_ms = 100u + (d * 300u) / 127u + (d2 * 1200u) / 127u;
        av->decay_coeff = decay_ms_to_q16(shell_ms);
        av->decay_coeff2 = decay_ms_to_q16(wires_ms);
        break;
    }
    case LOOM_DRUM_CLAP: {
        uint32_t tail_ms = 100u + (d * 350u) / 127u + (d2 * 1350u) / 127u;
        av->decay_coeff = decay_ms_to_q16(tail_ms);
        av->decay_coeff2 = decay_ms_to_q16(12u); /* 12ms burst */
        break;
    }
    case LOOM_DRUM_HAT_CL: {
        uint32_t base_q8 = (uint32_t)((280 * (213 + tu)) << 8) / 213u;
        uint32_t hat_ms = 30u + (d * 50u) / 127u + (d2 * 170u) / 127u;
        av->decay_coeff = decay_ms_to_q16(hat_ms);
        for (int i = 0; i < 6; i++) {
            av->hat_phase[i] = 0;
            av->hat_inc[i] = hz_to_inc_q8((base_q8 * HAT_RATIOS_Q8[i]) >> 8);
        }
        break;
    }
    case LOOM_DRUM_HAT_OP: {
        uint32_t base_q8 = (uint32_t)((280 * (213 + tu)) << 8) / 213u;
        uint32_t hat_ms = 200u + (d * 500u) / 127u + (d2 * 1700u) / 127u;
        av->decay_coeff = decay_ms_to_q16(hat_ms);
        for (int i = 0; i < 6; i++) {
            av->hat_phase[i] = 0;
            av->hat_inc[i] = hz_to_inc_q8((base_q8 * HAT_RATIOS_Q8[i]) >> 8);
        }
        break;
    }
    case LOOM_DRUM_CYMBAL: {
        uint32_t base_q8 = (uint32_t)((240 * (213 + tu)) << 8) / 213u;
        uint32_t cym_ms = 400u + (d * 900u) / 127u + (d2 * 3200u) / 127u;
        av->decay_coeff = decay_ms_to_q16(cym_ms);
        for (int i = 0; i < 6; i++) {
            av->hat_phase[i] = 0;
            av->hat_inc[i] = hz_to_inc_q8((base_q8 * CYM_RATIOS_Q8[i]) >> 8);
        }
        if (sn > 6u) {
            av->env = 0;
            av->attack_phase = 1;
            uint32_t atk_samples = 132u + (sn * 3528u) / 127u;
            av->attack_step = (int32_t)((1u << 24) / atk_samples);
        }
        break;
    }
    case LOOM_DRUM_PERC: {
        uint32_t f0_q8 = (uint32_t)((120 * (80 + tu)) << 8) / 80u;
        av->base_inc = hz_to_inc_q8(f0_q8);
        uint32_t perc_ms = 80u + (d * 370u) / 127u + (d2 * 1750u) / 127u;
        av->decay_coeff = decay_ms_to_q16(perc_ms);
        av->pitch_decay_coeff = decay_ms_to_q16(35u);
        break;
    }
    case LOOM_DRUM_NOISE: {
        uint32_t noise_ms = 60u + (d * 340u) / 127u + (d2 * 1600u) / 127u;
        av->decay_coeff = decay_ms_to_q16(noise_ms);
        break;
    }
    }

    /* ---------------- Trigger FM Voice ---------------- */
    fm_voice_t *fv = &f_voice[lane];
    fv->active = 1;
    fv->vel = (uint8_t)vel;
    fv->phase_c = 0;
    fv->phase_m = 0;
    fv->last_mod = 0;
    fv->env_c = (1 << 24);
    fv->env_m = (1 << 24);
    fv->pitch_env = (1 << 24);

    uint32_t base_c_q8 = 50u << 8;
    uint32_t ratio_m_q8 = 256u;
    int32_t  mod_lvl = 16384;
    int32_t  fb_lvl = 0;
    uint32_t c_ms = 200u;
    uint32_t m_ms = 100u;
    uint32_t p_ms = 30u;

    switch (lane) {
    case LOOM_DRUM_KICK:
        base_c_q8 = (uint32_t)((45 * (160 + tu)) << 8) / 160u;
        ratio_m_q8 = 128u; /* 0.5x sub-octave weight */
        mod_lvl = 26214 + (int32_t)((pu * 19660u) / 127u);
        fb_lvl = 0;
        c_ms = 150u + (d * 800u) / 127u;
        m_ms = 50u + (sn * 150u) / 127u;
        p_ms = 25u;
        break;
    case LOOM_DRUM_SNARE:
        base_c_q8 = (uint32_t)((160 * (160 + tu)) << 8) / 160u;
        ratio_m_q8 = 451u; /* 1.76x inharmonic membrane */
        mod_lvl = 21299 + (int32_t)((pu * 16384u) / 127u);
        fb_lvl = 16384 + (int32_t)((t * 14745u) / 127u);
        c_ms = 150u + (d * 500u) / 127u;
        m_ms = 100u + (sn * 300u) / 127u;
        p_ms = 20u;
        break;
    case LOOM_DRUM_CLAP: /* Tom in FM engine */
        base_c_q8 = (uint32_t)((90 * (128 + tu)) << 8) / 128u;
        ratio_m_q8 = 192u; /* 0.75x */
        mod_lvl = 16384;
        fb_lvl = 0;
        c_ms = 150u + (d * 600u) / 127u;
        m_ms = 120u;
        p_ms = 40u;
        break;
    case LOOM_DRUM_HAT_CL:
        base_c_q8 = (uint32_t)((400 * (213 + tu)) << 8) / 213u;
        ratio_m_q8 = 870u; /* 3.4x */
        mod_lvl = 26214;
        fb_lvl = 24576 + (int32_t)((t * 6553u) / 127u);
        c_ms = 40u + (d * 100u) / 127u;
        m_ms = 30u + (d * 80u) / 127u;
        break;
    case LOOM_DRUM_HAT_OP:
        base_c_q8 = (uint32_t)((400 * (213 + tu)) << 8) / 213u;
        ratio_m_q8 = 870u; /* 3.4x */
        mod_lvl = 26214;
        fb_lvl = 24576 + (int32_t)((t * 6553u) / 127u);
        c_ms = 200u + (d * 800u) / 127u;
        m_ms = 200u + (d * 800u) / 127u;
        break;
    case LOOM_DRUM_CYMBAL:
        base_c_q8 = (uint32_t)((300 * (213 + tu)) << 8) / 213u;
        ratio_m_q8 = 371u; /* 1.45x */
        mod_lvl = 29491 + (int32_t)((pu * 13107u) / 127u);
        fb_lvl = 21299 + (int32_t)((t * 9830u) / 127u);
        c_ms = 500u + (d * 1500u) / 127u;
        m_ms = 500u + (d * 1500u) / 127u;
        break;
    case LOOM_DRUM_PERC:
        base_c_q8 = (uint32_t)((550 * (107 + tu)) << 8) / 107u;
        ratio_m_q8 = 384u; /* 1.5x hollow square */
        mod_lvl = 19660;
        fb_lvl = 0;
        c_ms = 100u + (d * 400u) / 127u;
        m_ms = 80u;
        break;
    case LOOM_DRUM_NOISE:
        base_c_q8 = 100u << 8;
        ratio_m_q8 = 4941u; /* 19.3x */
        mod_lvl = 32767;
        fb_lvl = 31129;
        c_ms = 80u + (d * 500u) / 127u;
        m_ms = 80u + (d * 500u) / 127u;
        break;
    }

    fv->inc_c = hz_to_inc_q8(base_c_q8);
    fv->inc_m = hz_to_inc_q8((base_c_q8 * ratio_m_q8) >> 8);
    fv->mod_level = mod_lvl;
    fv->feedback = fb_lvl;
    fv->decay_c = decay_ms_to_q16(c_ms);
    fv->decay_m = decay_ms_to_q16(m_ms);
    fv->decay_pitch = decay_ms_to_q16(p_ms);
}

/* Render block of N samples */
void loom_drum_render(int32_t *out, uint32_t n, uint32_t kit, const int16_t *p)
{
    (void)p;
    if (kit == LOOM_KIT_FM) {
        /* ---------------- Render Loom FM Drums ---------------- */
        for (uint32_t l = 0; l < LOOM_DRUM_LANES; l++) {
            fm_voice_t *v = &f_voice[l];
            if (!v->active) continue;

            int32_t vel_scale = (int32_t)v->vel; /* 1..127 */
            for (uint32_t i = 0; i < n; i++) {
                /* Modulator with feedback */
                int32_t fb_phase = ((int64_t)v->last_mod * v->feedback) >> 15;
                int32_t mod_val = sine_i(v->phase_m + (uint32_t)(fb_phase << 16));
                v->last_mod = mod_val;

                /* Carrier frequency modulated */
                int32_t m_signal = ((int64_t)mod_val * v->mod_level) >> 15;
                m_signal = ((int64_t)m_signal * (v->env_m >> 9)) >> 15; /* env_m in Q15 */
                int32_t car_val = sine_i(v->phase_c + (uint32_t)(m_signal << 16));

                /* Scale carrier by envelope and velocity */
                int32_t sample = ((int64_t)car_val * (v->env_c >> 9)) >> 15;
                sample = (sample * vel_scale) >> 7;
                out[i] += sample;

                /* Pitch sweep for kick/snare/tom */
                uint32_t cur_inc_c = v->inc_c;
                uint32_t cur_inc_m = v->inc_m;
                if (v->pitch_env > (1 << 16)) {
                    uint32_t sweep_mult = 1u + (uint32_t)(v->pitch_env >> 22); /* 1x..4x */
                    cur_inc_c *= sweep_mult;
                    cur_inc_m *= sweep_mult;
                    v->pitch_env = ((int64_t)v->pitch_env * v->decay_pitch) >> 16;
                }

                v->phase_c += cur_inc_c;
                v->phase_m += cur_inc_m;
                v->env_c = ((int64_t)v->env_c * v->decay_c) >> 16;
                v->env_m = ((int64_t)v->env_m * v->decay_m) >> 16;

                if (v->env_c < 4096) {
                    v->active = 0;
                    break;
                }
            }
        }
    } else {
        /* ---------------- Render Loom Analog Drums ---------------- */
        for (uint32_t l = 0; l < LOOM_DRUM_LANES; l++) {
            analog_voice_t *v = &a_voice[l];
            if (!v->active) continue;

            int32_t vel_scale = (int32_t)v->vel; /* 1..127 */
            for (uint32_t i = 0; i < n; i++) {
                int32_t sample = 0;

                switch (v->type) {
                case LOOM_DRUM_KICK: {
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    v->pitch_env = ((int64_t)v->pitch_env * v->decay_coeff2) >> 16;
                    if (v->env < 2048) { v->active = 0; break; }

                    /* Pitch sweep: f0 * (1 + punch * pitch_env) */
                    uint32_t punch_factor = 1u + ((uint32_t)(v->punch) * (uint32_t)(v->pitch_env >> 16) >> 14);
                    v->phase += v->base_inc * punch_factor;
                    int32_t s = sine_i(v->phase);

                    /* Tone saturation */
                    if (v->tone > 10) {
                        int32_t drive = 32768 + (v->tone << 9);
                        int64_t x = ((int64_t)s * drive) >> 15;
                        s = loom_clamp((int32_t)x, -32000, 32000);
                    }
                    sample = ((int64_t)s * (v->env >> 9)) >> 15;
                    break;
                }
                case LOOM_DRUM_SNARE: {
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    v->env2 = ((int64_t)v->env2 * v->decay_coeff2) >> 16;
                    if (v->env < 2048 && v->env2 < 2048) { v->active = 0; break; }

                    v->phase += v->base_inc;
                    int32_t shell1 = sine_i(v->phase);
                    int32_t shell2 = (sine_i(v->phase + (v->phase >> 1) + (v->phase >> 5)) * 13107) >> 15;
                    int32_t shell = shell1 + shell2;

                    int32_t noise = loom_rng();
                    int32_t hp_coeff = 4915 + (v->tone << 8);
                    v->filter_state += ((noise - v->filter_state) * hp_coeff) >> 15;
                    int32_t wires = ((noise - v->filter_state) * (v->env2 >> 9)) >> 15;

                    int32_t snap_amt = v->snap << 8; /* 0..32768 */
                    int32_t s_shell = ((int64_t)shell * (32768 - (snap_amt >> 1))) >> 15;
                    s_shell = ((int64_t)s_shell * (v->env >> 9)) >> 15;
                    int32_t s_wires = ((int64_t)wires * (8192 + snap_amt)) >> 15;
                    sample = (s_shell + s_wires);
                    break;
                }
                case LOOM_DRUM_CLAP: {
                    v->clap_timer--;
                    if (v->clap_stage < 3 && v->clap_timer <= 0) {
                        v->clap_env = (1 << 24);
                        v->clap_timer = 353 + (v->snap << 2) + (loom_rng() >> 11);
                        v->clap_stage++;
                    }
                    v->clap_env = ((int64_t)v->clap_env * v->decay_coeff2) >> 16;
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    if (v->env < 2048 && v->clap_stage >= 3) { v->active = 0; break; }

                    int32_t noise = loom_rng();
                    int32_t bp_coeff = 10485 + (v->tone << 7);
                    v->filter_state += ((noise - v->filter_state) * bp_coeff) >> 15;
                    int32_t bp = noise - v->filter_state;
                    int32_t mixed_env = ((v->clap_env >> 9) * 26214 + (v->env >> 9) * 13107) >> 15;
                    sample = ((int64_t)bp * mixed_env) >> 15;
                    break;
                }
                case LOOM_DRUM_HAT_CL:
                case LOOM_DRUM_HAT_OP: {
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    if (v->env < 2048) { v->active = 0; break; }

                    int32_t cluster = 0;
                    for (int k = 0; k < 6; k++) {
                        v->hat_phase[k] += v->hat_inc[k];
                        cluster += (v->hat_phase[k] > 0x80000000u) ? 5460 : -5460;
                    }
                    /* 2-pole highpass */
                    int32_t hp_coeff = 14745 + (v->tone << 7);
                    v->filter_state += ((cluster - v->filter_state) * hp_coeff) >> 15;
                    int32_t hp1 = cluster - v->filter_state;
                    v->filter_state2 += ((hp1 - v->filter_state2) * hp_coeff) >> 15;
                    int32_t hp2 = hp1 - v->filter_state2;

                    sample = ((int64_t)hp2 * (v->env >> 9)) >> 14;
                    break;
                }
                case LOOM_DRUM_CYMBAL: {
                    if (v->attack_phase) {
                        v->env += v->attack_step;
                        if (v->env >= (1 << 24)) {
                            v->env = (1 << 24);
                            v->attack_phase = 0;
                        }
                    } else {
                        v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    }
                    if (v->env < 2048) { v->active = 0; break; }

                    int32_t cluster = 0;
                    for (int k = 0; k < 6; k++) {
                        v->hat_phase[k] += v->hat_inc[k];
                        cluster += (v->hat_phase[k] > 0x80000000u) ? 5460 : -5460;
                    }
                    int32_t hp_coeff = 8000 + (v->tone << 7);
                    v->filter_state += ((cluster - v->filter_state) * hp_coeff) >> 15;
                    int32_t hp1 = cluster - v->filter_state;
                    v->filter_state2 += ((hp1 - v->filter_state2) * hp_coeff) >> 15;
                    int32_t hp2 = hp1 - v->filter_state2;

                    sample = ((int64_t)hp2 * (v->env >> 9)) >> 14;
                    break;
                }
                case LOOM_DRUM_PERC: {
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    if (v->env < 2048) { v->active = 0; break; }

                    v->phase += v->base_inc;
                    int32_t s = sine_i(v->phase);
                    if (v->tone > 40) {
                        s = (s + ((sine_i(v->phase + (v->phase >> 1) - (v->phase >> 6)) * 11468) >> 15));
                    }
                    sample = ((int64_t)s * (v->env >> 9)) >> 15;
                    break;
                }
                case LOOM_DRUM_NOISE: {
                    v->env = ((int64_t)v->env * v->decay_coeff) >> 16;
                    if (v->env < 2048) { v->active = 0; break; }

                    int32_t noise = loom_rng();
                    int32_t lp_coeff = 2621 + (v->tone << 8);
                    v->filter_state += ((noise - v->filter_state) * lp_coeff) >> 15;
                    sample = ((int64_t)v->filter_state * (v->env >> 9)) >> 15;
                    break;
                }
                }

                sample = (sample * vel_scale) >> 7;
                out[i] += sample;
            }
        }
    }
}
