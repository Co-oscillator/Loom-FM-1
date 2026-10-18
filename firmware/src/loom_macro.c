/* SPDX-License-Identifier: GPL-3.0-only
 * Loom FM-1: Macro System Implementation
 */
#include "loom_macro.h"

macro_t loom_macros[NMACROS];
uint8_t macro_ui_line = 0;

void macro_init(void)
{
    macro_ui_line = 0;
    for (uint32_t m = 0; m < NMACROS; m++) {
        loom_macros[m].src = MS_KNOB;
        loom_macros[m].val = 64;
        for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
            loom_macros[m].dest[d].trk = (uint8_t)(m < NTRK ? m : 0);
            loom_macros[m].dest[d].dest = 0; /* OFF */
            loom_macros[m].dest[d].amt = 0;
        }
    }
    macro_sync_to_tracks();
}

void macro_sync_to_tracks(void)
{
    for (uint32_t m = 0; m < NMACROS && m < NTRK; m++) {
        macro_t *mc = &loom_macros[m];
        track_t *t = &trk[m];
        t->p[P_M1SRC] = mc->src;
        t->p[P_M1DST] = mc->val;
        t->p[P_M1AMT] = mc->dest[0].amt;
        t->p[P_M2SRC] = mc->dest[0].trk;
        t->p[P_M2DST] = mc->dest[0].dest;
        t->p[P_M2AMT] = mc->dest[1].amt;
        t->p[P_M3SRC] = mc->dest[1].trk;
        t->p[P_M3DST] = mc->dest[1].dest;
        t->p[P_M3AMT] = mc->dest[2].amt;
        t->p[P_M4SRC] = mc->dest[2].trk;
        t->p[P_M4DST] = mc->dest[2].dest;
        t->p[P_M4AMT] = 0;
    }
}

void macro_sync_from_tracks(void)
{
    for (uint32_t m = 0; m < NMACROS && m < NTRK; m++) {
        macro_t *mc = &loom_macros[m];
        const track_t *t = &trk[m];
        mc->src = (uint8_t)clamp(t->p[P_M1SRC], 0, MS_N - 1);
        if (!mc->src) mc->src = MS_KNOB;
        mc->val = (uint8_t)clamp(t->p[P_M1DST], 0, 127);
        mc->dest[0].amt = (int8_t)clamp(t->p[P_M1AMT], -64, 63);
        mc->dest[0].trk = (uint8_t)clamp(t->p[P_M2SRC], 0, NTRK - 1);
        mc->dest[0].dest = (uint8_t)clamp(t->p[P_M2DST], 0, 14);
        mc->dest[1].amt = (int8_t)clamp(t->p[P_M2AMT], -64, 63);
        mc->dest[1].trk = (uint8_t)clamp(t->p[P_M3SRC], 0, NTRK - 1);
        mc->dest[1].dest = (uint8_t)clamp(t->p[P_M3DST], 0, 14);
        mc->dest[2].amt = (int8_t)clamp(t->p[P_M3AMT], -64, 63);
        mc->dest[2].trk = (uint8_t)clamp(t->p[P_M4SRC], 0, NTRK - 1);
        mc->dest[2].dest = (uint8_t)clamp(t->p[P_M4DST], 0, 14);
    }
}

const char *macro_trk_name(uint8_t trk_idx)
{
    static const char *const TRK_NAMES[] = {"T1", "T2", "T3", "T4", "DRM"};
    return TRK_NAMES[trk_idx < 5 ? trk_idx : 0];
}

const char *macro_dest_name(uint8_t trk_idx, uint8_t dest_id)
{
    static const char *const COMMON[] = {
        "OFF", "PITCH", "CUT", "AMP", "PAN", "DLY", "REV"
    };
    if (dest_id == 0) return "OFF";
    if (dest_id <= 6) return COMMON[dest_id];
    if (dest_id >= 7 && dest_id <= 14) {
        uint8_t ep = dest_id - 7;
        if (trk_idx == 4) {
            static const char *const DRUM_NAMES[] = {
                "KICK", "SNAR", "CLAP", "HATS", "CYMB", "TOM", "PERC", "BELL"
            };
            return DRUM_NAMES[ep];
        }
        const track_t *t = &trk[trk_idx < NTRK ? trk_idx : 0];
        const param_desc_t *pd = track_desc(t, P_E0 + ep);
        if (pd && pd->label && pd->label[0] && pd->label[0] != '-')
            return pd->label;
        static char buf[4];
        buf[0] = 'E'; buf[1] = '1' + ep; buf[2] = 0;
        return buf;
    }
    return "OFF";
}

int macro_is_assigned(uint32_t m)
{
    if (m >= NMACROS) return 0;
    const macro_t *mc = &loom_macros[m];
    if (mc->src != MS_KNOB) return 0;
    for (uint32_t d = 0; d < NMACRO_DESTS; d++) {
        if (mc->dest[d].dest != 0) return 1;
    }
    return 0;
}

